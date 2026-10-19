/*
  calendar.cpp

  Copyright (c) 2026 Leigh Johnston.  All Rights Reserved.

  This program is free software: you can redistribute it and / or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.

  Portions of this file are derived from CPython 3.13 (Lib/calendar.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module calendar (CPython 3.13's classes, functions, behaviour and messages): the methods of its classes call each other through
// self, so a subclass's overrides are used as CPython's are

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>
#include "objects.hpp"

namespace neos::python_runtime
{
    namespace detail
    {
        namespace
        {
            struct calendar_state
            {
                object* month = nullptr;        // (the enumerations Month and Day)
                object* day = nullptr;
                object* dayName = nullptr;      // (day_name, day_abbr, month_name and month_abbr)
                object* dayAbbr = nullptr;
                object* monthName = nullptr;
                object* monthAbbr = nullptr;
                object* textCalendar = nullptr;
                object* htmlCalendar = nullptr;
                object* differentLocale = nullptr;
            } s;

            // --- helpers

            long long floor_mod(long long a, long long b) { auto const r = a % b; return r != 0 && ((r < 0) != (b < 0)) ? r + b : r; }
            long long floor_div(long long a, long long b) { auto const q = a / b; return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q; }

            // (str.center's padding)
            std::string centered(std::string const& aText, long long aWidth)
            {
                auto const length = static_cast<long long>(code_point_count(aText));
                auto const margin = aWidth - length;
                if (margin <= 0)
                    return aText;
                auto const left = margin / 2 + (margin & aWidth & 1);
                return std::string(static_cast<std::size_t>(left), ' ') + aText + std::string(static_cast<std::size_t>(margin - left), ' ');
            }

            std::string right_stripped(std::string const& aText)
            {
                auto points = to_code_points(aText);
                while (!points.empty() && char_is_space(points.back()))
                    points.pop_back();
                return from_code_points(points);
            }

            std::string truncated(std::string const& aText, long long aWidth)    // (text[:width])
            {
                auto const points = to_code_points(aText);
                auto const length = static_cast<long long>(points.size());
                auto end = aWidth < 0 ? std::max(0LL, length + aWidth) : std::min(aWidth, length);
                return from_code_points(std::u32string_view{ points }.substr(0u, static_cast<std::size_t>(end)));
            }

            std::string repeated(char const* aText, long long aCount)
            {
                std::string result;
                for (long long i = 0; i < aCount; ++i)
                    result += aText;
                return result;
            }

            // (a str result of a method that must return one)
            bool str_result(object* aObject, std::string& aResult)
            {
                if (aObject == nullptr)
                    return false;
                auto const text = str_of_value(aObject);
                if (text == nullptr)
                    return (error("TypeError", std::string{ "sequence item: expected str instance, " } + type_name(aObject) + " found"), false);
                aResult = *text;
                return true;
            }

            object* method(object* aSelf, char const* aName, std::vector<object*> aArgs)
            {
                return call_method_named(aSelf, aName, std::move(aArgs));
            }

            object* date_of(object* aYear, object* aMonth, object* aDay)  // (datetime.date(year, month, day): borrowed arguments)
            {
                ref const date{ module_attribute("datetime", "date") };
                return date ? invoke(date.get(), { new_reference(aYear), new_reference(aMonth), new_reference(aDay) }) : nullptr;
            }

            bool print_text(std::string const& aText, bool aNewline)
            {
                ref const print{ fn_builtin("print") };
                ref const end{ make_str(aNewline ? "\n" : "") };
                ref const kw{ keywords({ { "end", end.get() } }) };
                ref const result{ invoke(print.get(), { make_str(aText) }, kw.get()) };
                return static_cast<bool>(result);
            }

            std::nullptr_t illegal_month(object* aMonth)
            {
                ref const cls{ module_attribute("calendar", "IllegalMonthError") };
                if (cls)
                    if (ref const e{ invoke(cls.get(), { new_reference(aMonth) }) })
                        fn_raise(e.get());
                return nullptr;
            }

            // (_validate_month: the month's number)
            bool validate_month(object* aMonth, long long& aResult)
            {
                if (!to_index(aMonth, aResult))
                    return false;
                if (aResult < 1 || aResult > 12)
                    return (illegal_month(aMonth), false);
                return true;
            }

            bool is_leap(long long aYear) { return floor_mod(aYear, 4) == 0 && (floor_mod(aYear, 100) != 0 || floor_mod(aYear, 400) == 0); }

            long long month_length(long long aYear, long long aMonth)
            {
                static int const sDays[] = { 0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
                return sDays[aMonth] + (aMonth == 2 && is_leap(aYear) ? 1 : 0);
            }

            // (weekday(year, month, day): a Day)
            object* weekday_of(object* aYear, object* aMonth, object* aDay)
            {
                long long year = 0;
                if (!to_index(aYear, year))
                    return nullptr;
                ref yearValue{ new_reference(aYear) };
                if (year < 1 || year > 9999)
                    yearValue = ref{ make_int(2000 + floor_mod(year, 400)) };
                ref const date{ date_of(yearValue.get(), aMonth, aDay) };
                if (!date)
                    return nullptr;
                ref const weekday{ method(date.get(), "weekday", {}) };
                return weekday ? invoke(s.day, { new_reference(weekday.get()) }) : nullptr;
            }

            // (monthrange(year, month): the first day's weekday (a Day) and the number of days)
            bool month_range(object* aYear, object* aMonth, ref& aFirst, long long& aDays)
            {
                long long month = 0;
                if (!validate_month(aMonth, month))
                    return false;
                ref const one{ make_int(1) };
                aFirst = ref{ weekday_of(aYear, aMonth, one.get()) };
                if (!aFirst)
                    return false;
                long long year = 0;
                if (!to_index(aYear, year))
                    return false;
                aDays = month_length(year, month);
                return true;
            }

            bool first_weekday(object* aSelf, long long& aResult) { return attribute_int(aSelf, "firstweekday", aResult); }

            // (a method's iterable result's items)
            bool items_of(object* aIterable, std::vector<object*>& aItems)
            {
                if (aIterable == nullptr)
                    return false;
                return collect(aIterable, aItems);
            }

            void release_all(std::vector<object*>& aItems)
            {
                for (auto i : aItems)
                    release(i);
                aItems.clear();
            }

            // ([items[i:i+n] for i in range(0, len(items), n)]: steals the items)
            object* chunked(std::vector<object*> aItems, long long aWidth)
            {
                if (aWidth == 0)
                {
                    release_all(aItems);
                    return error("ValueError", "range() arg 3 must not be zero");
                }
                std::vector<object*> rows;
                if (aWidth > 0)
                    for (std::size_t i = 0u; i < aItems.size(); i += static_cast<std::size_t>(aWidth))
                    {
                        auto const end = std::min(aItems.size(), i + static_cast<std::size_t>(aWidth));
                        std::vector<object*> row;
                        for (auto j = i; j < end; ++j)
                            row.push_back(new_reference(aItems[j]));
                        rows.push_back(make_list(std::move(row)));
                    }
                release_all(aItems);
                return make_list(std::move(rows));
            }

            // (formatstring(cols, colwidth, spacing))
            bool format_columns(object* aColumns, long long aWidth, long long aSpacing, std::string& aResult)
            {
                std::vector<object*> columns;
                if (!collect(aColumns, columns))
                    return false;
                aResult.clear();
                bool ok = true;
                for (std::size_t i = 0u; i < columns.size() && ok; ++i)
                {
                    std::string text;
                    if (auto const t = str_of_value(columns[i]))
                        text = centered(*t, aWidth);
                    else
                    {
                        ref const width{ make_int(aWidth) };
                        ref const c{ method(columns[i], "center", { new_reference(width.get()) }) };
                        ok = str_result(c.get(), text);
                    }
                    if (i != 0u)
                        aResult += repeated(" ", aSpacing);
                    aResult += text;
                }
                release_all(columns);
                return ok;
            }

            bool format_strings(std::vector<std::string> const& aColumns, long long aWidth, long long aSpacing, std::string& aResult)
            {
                std::vector<object*> items;
                for (auto const& c : aColumns)
                    items.push_back(make_str(c));
                ref const list{ make_list(std::move(items)) };
                return format_columns(list.get(), aWidth, aSpacing, aResult);
            }

            // (an optional argument's integer value)
            bool int_argument(object* aArgument, long long aDefault, long long& aResult)
            {
                if (aArgument == nullptr)
                    return (aResult = aDefault, true);
                return to_index(aArgument, aResult);
            }

            // --- _localized_month and _localized_day

            object* localized_name(object* aSelf, long long aIndex, bool aMonths)
            {
                ref const format{ getattr(aSelf, "format") };
                if (!format)
                    return nullptr;
                if (aMonths && aIndex == 0)
                    return make_str("");
                ref const year{ make_int(2001) };
                ref const month{ make_int(aMonths ? aIndex : 1) };
                ref const day{ make_int(aMonths ? 1 : aIndex + 1) };
                ref const date{ date_of(year.get(), month.get(), day.get()) };
                return date ? method(date.get(), "strftime", { new_reference(format.get()) }) : nullptr;
            }

            object* localized_item(std::vector<object*> const& aArgs, bool aMonths)
            {
                if (aArgs.size() != 2u)
                    return error("TypeError", "__getitem__() takes 2 positional arguments but " + std::to_string(aArgs.size()) + " were given");
                long long const count = aMonths ? 13 : 7;
                if (kind_of(aArgs[1]) == NEOS_PY_SLICE)
                {
                    long long start = 0, stop = 0, step = 0;
                    if (!slice_indices(aArgs[1], count, start, stop, step))
                        return nullptr;
                    std::vector<object*> names;
                    for (auto i = start; step > 0 ? i < stop : i > stop; i += step)
                    {
                        auto const name = localized_name(aArgs[0], i, aMonths);
                        if (name == nullptr)
                        {
                            release_all(names);
                            return nullptr;
                        }
                        names.push_back(name);
                    }
                    return make_list(std::move(names));
                }
                long long index = 0;
                if (!to_index(aArgs[1], index))
                {
                    clear_error();
                    return error("TypeError", std::string{ "list indices must be integers or slices, not " } + type_name(aArgs[1]));
                }
                if (index < 0)
                    index += count;
                if (index < 0 || index >= count)
                    return error("IndexError", "list index out of range");
                return localized_name(aArgs[0], index, aMonths);
            }

            object* localized_init(std::vector<object*> const& aArgs, object* aKwargs, char const* aName)
            {
                std::vector<object*> a;
                if (!self_arguments(aName, aArgs, aKwargs, { "format" }, 1u, 0u, a))
                    return nullptr;
                return setattr(aArgs[0], "format", a[0]) < 0 ? nullptr : new_reference(none());
            }

            builtin_entry const sLocalizedMonthMethods[] =
            {
                { "__init__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return localized_init(aArgs, aKwargs, "_localized_month"); } },
                { "__getitem__", [](std::vector<object*> const& aArgs, object*) -> object* { return localized_item(aArgs, true); } },
                { "__len__", [](std::vector<object*> const&, object*) -> object* { return make_int(13); } }
            };

            builtin_entry const sLocalizedDayMethods[] =
            {
                { "__init__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return localized_init(aArgs, aKwargs, "_localized_day"); } },
                { "__getitem__", [](std::vector<object*> const& aArgs, object*) -> object* { return localized_item(aArgs, false); } },
                { "__len__", [](std::vector<object*> const&, object*) -> object* { return make_int(7); } }
            };

            // --- the exceptions

            builtin_entry const sIllegalMonthMethods[] =
            {
                { "__init__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("IllegalMonthError.__init__", aArgs, aKwargs, { "month" }, 1u, 0u, a))
                            return nullptr;
                        return setattr(aArgs[0], "month", a[0]) < 0 ? nullptr : new_reference(none());
                    } },
                { "__str__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        ref const month{ getattr(aArgs[0], "month") };
                        return month ? make_str("bad month number " + repr_text(month.get()) + "; must be 1-12") : nullptr;
                    } }
            };

            builtin_entry const sIllegalWeekdayMethods[] =
            {
                { "__init__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("IllegalWeekdayError.__init__", aArgs, aKwargs, { "weekday" }, 1u, 0u, a))
                            return nullptr;
                        return setattr(aArgs[0], "weekday", a[0]) < 0 ? nullptr : new_reference(none());
                    } },
                { "__str__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        ref const weekday{ getattr(aArgs[0], "weekday") };
                        return weekday ? make_str("bad weekday number " + repr_text(weekday.get()) + "; must be 0 (Monday) to 6 (Sunday)") : nullptr;
                    } }
            };

            // --- Calendar

            // (itermonthdays3: (year, month, day) tuples)
            bool month_days3(object* aSelf, object* aYear, object* aMonth, std::vector<object*>& aResult)
            {
                ref first;
                long long days = 0;
                if (!month_range(aYear, aMonth, first, days))
                    return false;
                long long day1 = 0, firstWeekday = 0, year = 0, month = 0;
                if (!to_index(first.get(), day1) || !first_weekday(aSelf, firstWeekday) || !to_index(aYear, year) || !to_index(aMonth, month))
                    return false;
                auto const before = floor_mod(day1 - firstWeekday, 7);
                auto const after = floor_mod(firstWeekday - day1 - days, 7);
                auto const py = month == 1 ? year - 1 : year;
                auto const pm = month == 1 ? 12 : month - 1;
                auto const end = month_length(py, pm) + 1;
                for (auto d = end - before; d < end; ++d)
                    aResult.push_back(make_tuple({ make_int(py), make_int(pm), make_int(d) }));
                for (long long d = 1; d <= days; ++d)
                    aResult.push_back(make_tuple({ new_reference(aYear), new_reference(aMonth), make_int(d) }));
                auto const ny = month == 12 ? year + 1 : year;
                auto const nm = month == 12 ? 1 : month + 1;
                for (long long d = 1; d <= after; ++d)
                    aResult.push_back(make_tuple({ make_int(ny), make_int(nm), make_int(d) }));
                return true;
            }

            bool year_month_arguments(char const* aName, std::vector<object*> const& aArgs, object* aKwargs, std::vector<object*>& a)
            {
                return self_arguments(aName, aArgs, aKwargs, { "year", "month" }, 2u, 0u, a);
            }

            // (a matrix of a month (monthdatescalendar etc.): rows of 7 of the items of the iterator method aIterator)
            object* month_matrix(char const* aName, char const* aIterator, std::vector<object*> const& aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                if (!year_month_arguments(aName, aArgs, aKwargs, a))
                    return nullptr;
                ref const iterable{ method(aArgs[0], aIterator, { new_reference(a[0]), new_reference(a[1]) }) };
                std::vector<object*> items;
                if (!items_of(iterable.get(), items))
                    return nullptr;
                return chunked(std::move(items), 7);
            }

            // (a year's data (yeardatescalendar etc.): rows of width months of the method aMonthMethod's matrices)
            object* year_matrix(char const* aName, char const* aMonthMethod, std::vector<object*> const& aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                if (!self_arguments(aName, aArgs, aKwargs, { "year", "width" }, 1u, 0u, a))
                    return nullptr;
                long long width = 0;
                if (!int_argument(a[1], 3, width))
                    return nullptr;
                std::vector<object*> months;
                if (!collect(s.month, months))
                    return nullptr;
                std::vector<object*> matrices;
                for (auto m : months)
                {
                    auto const matrix = method(aArgs[0], aMonthMethod, { new_reference(a[0]), new_reference(m) });
                    if (matrix == nullptr)
                    {
                        release_all(months);
                        release_all(matrices);
                        return nullptr;
                    }
                    matrices.push_back(matrix);
                }
                release_all(months);
                return chunked(std::move(matrices), width);
            }

            builtin_entry const sCalendarMethods[] =
            {
                { "__init__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("Calendar.__init__", aArgs, aKwargs, { "firstweekday" }, 0u, 0u, a))
                            return nullptr;
                        ref const value{ a[0] != nullptr ? new_reference(a[0]) : make_int(0) };
                        return setattr(aArgs[0], "firstweekday", value.get()) < 0 ? nullptr : new_reference(none());
                    } },
                { "getfirstweekday", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("getfirstweekday", aArgs, aKwargs, {}, 0u, 0u, a))
                            return nullptr;
                        ref const value{ getattr(aArgs[0], "_firstweekday") };
                        ref const seven{ make_int(7) };
                        return value ? binary(NEOS_PY_REMAINDER, value.get(), seven.get(), false) : nullptr;
                    } },
                { "setfirstweekday", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("setfirstweekday", aArgs, aKwargs, { "firstweekday" }, 1u, 0u, a))
                            return nullptr;
                        return setattr(aArgs[0], "_firstweekday", a[0]) < 0 ? nullptr : new_reference(none());
                    } },
                { "iterweekdays", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long first = 0;
                        if (!self_arguments("iterweekdays", aArgs, aKwargs, {}, 0u, 0u, a) || !first_weekday(aArgs[0], first))
                            return nullptr;
                        std::vector<object*> days;
                        for (auto i = first; i < first + 7; ++i)
                            days.push_back(make_int(floor_mod(i, 7)));
                        ref const list{ make_list(std::move(days)) };
                        return iter(list.get());
                    } },
                { "itermonthdates", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!year_month_arguments("itermonthdates", aArgs, aKwargs, a))
                            return nullptr;
                        ref const iterable{ method(aArgs[0], "itermonthdays3", { new_reference(a[0]), new_reference(a[1]) }) };
                        std::vector<object*> items;
                        if (!items_of(iterable.get(), items))
                            return nullptr;
                        std::vector<object*> dates;
                        for (auto i : items)
                        {
                            std::vector<object*> ymd;
                            if (!collect(i, ymd) || ymd.size() != 3u)
                            {
                                if (!failed())
                                    error("ValueError", ymd.size() > 3u ? "too many values to unpack (expected 3)" : "not enough values to unpack (expected 3, got " + std::to_string(ymd.size()) + ")");
                                release_all(ymd);
                                release_all(items);
                                release_all(dates);
                                return nullptr;
                            }
                            auto const date = date_of(ymd[0], ymd[1], ymd[2]);
                            release_all(ymd);
                            if (date == nullptr)
                            {
                                release_all(items);
                                release_all(dates);
                                return nullptr;
                            }
                            dates.push_back(date);
                        }
                        release_all(items);
                        ref const list{ make_list(std::move(dates)) };
                        return iter(list.get());
                    } },
                { "itermonthdays", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!year_month_arguments("itermonthdays", aArgs, aKwargs, a))
                            return nullptr;
                        ref first;
                        long long days = 0, day1 = 0, firstWeekday = 0;
                        if (!month_range(a[0], a[1], first, days) || !to_index(first.get(), day1) || !first_weekday(aArgs[0], firstWeekday))
                            return nullptr;
                        std::vector<object*> result;
                        for (long long i = floor_mod(day1 - firstWeekday, 7); i > 0; --i)
                            result.push_back(make_int(0));
                        for (long long d = 1; d <= days; ++d)
                            result.push_back(make_int(d));
                        for (long long i = floor_mod(firstWeekday - day1 - days, 7); i > 0; --i)
                            result.push_back(make_int(0));
                        ref const list{ make_list(std::move(result)) };
                        return iter(list.get());
                    } },
                { "itermonthdays2", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!year_month_arguments("itermonthdays2", aArgs, aKwargs, a))
                            return nullptr;
                        ref const iterable{ method(aArgs[0], "itermonthdays", { new_reference(a[0]), new_reference(a[1]) }) };
                        long long first = 0;
                        if (!iterable || !first_weekday(aArgs[0], first))
                            return nullptr;
                        std::vector<object*> items;
                        if (!items_of(iterable.get(), items))
                            return nullptr;
                        std::vector<object*> result;
                        auto i = first;
                        for (auto d : items)
                            result.push_back(make_tuple({ d, make_int(floor_mod(i++, 7)) }));
                        ref const list{ make_list(std::move(result)) };
                        return iter(list.get());
                    } },
                { "itermonthdays3", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!year_month_arguments("itermonthdays3", aArgs, aKwargs, a))
                            return nullptr;
                        std::vector<object*> result;
                        if (!month_days3(aArgs[0], a[0], a[1], result))
                        {
                            release_all(result);
                            return nullptr;
                        }
                        ref const list{ make_list(std::move(result)) };
                        return iter(list.get());
                    } },
                { "itermonthdays4", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!year_month_arguments("itermonthdays4", aArgs, aKwargs, a))
                            return nullptr;
                        ref const iterable{ method(aArgs[0], "itermonthdays3", { new_reference(a[0]), new_reference(a[1]) }) };
                        std::vector<object*> items;
                        if (!items_of(iterable.get(), items))
                            return nullptr;
                        std::vector<object*> result;
                        long long i = 0;
                        for (auto item : items)
                        {
                            std::vector<object*> ymd;
                            long long first = 0;
                            if (!collect(item, ymd) || ymd.size() != 3u || !first_weekday(aArgs[0], first))
                            {
                                if (!failed())
                                    error("ValueError", ymd.size() > 3u ? "too many values to unpack (expected 3)" : "not enough values to unpack (expected 3, got " + std::to_string(ymd.size()) + ")");
                                release_all(ymd);
                                release_all(items);
                                release_all(result);
                                return nullptr;
                            }
                            ymd.push_back(make_int(floor_mod(first + i++, 7)));
                            result.push_back(make_tuple(std::move(ymd)));
                        }
                        release_all(items);
                        ref const list{ make_list(std::move(result)) };
                        return iter(list.get());
                    } },
                { "monthdatescalendar", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return month_matrix("monthdatescalendar", "itermonthdates", aArgs, aKwargs); } },
                { "monthdays2calendar", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return month_matrix("monthdays2calendar", "itermonthdays2", aArgs, aKwargs); } },
                { "monthdayscalendar", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return month_matrix("monthdayscalendar", "itermonthdays", aArgs, aKwargs); } },
                { "yeardatescalendar", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return year_matrix("yeardatescalendar", "monthdatescalendar", aArgs, aKwargs); } },
                { "yeardays2calendar", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return year_matrix("yeardays2calendar", "monthdays2calendar", aArgs, aKwargs); } },
                { "yeardayscalendar", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return year_matrix("yeardayscalendar", "monthdayscalendar", aArgs, aKwargs); } }
            };

            // --- TextCalendar

            // (self.formatweek(theweek, width): a str)
            bool format_week(object* aSelf, object* aWeek, object* aWidth, std::string& aResult)
            {
                ref const text{ method(aSelf, "formatweek", { new_reference(aWeek), new_reference(aWidth) }) };
                return str_result(text.get(), aResult);
            }

            object* text_formatweekday(object* aSelf, object* aDay, object* aWidth)
            {
                long long width = 0;
                if (!to_index(aWidth, width))
                    return nullptr;
                ref const name{ getitem(width >= 9 ? s.dayName : s.dayAbbr, aDay) };
                std::string text;
                if (!str_result(name.get(), text))
                    return nullptr;
                (void)aSelf;
                return make_str(centered(truncated(text, width), width));
            }

            object* text_formatmonthname(object* aYear, object* aMonth, object* aWidth, object* aWithYear)
            {
                long long month = 0, width = 0;
                if (!validate_month(aMonth, month) || !to_index(aWidth, width))
                    return nullptr;
                ref const name{ getitem(s.monthName, aMonth) };
                std::string text;
                if (!str_result(name.get(), text))
                    return nullptr;
                if (aWithYear == nullptr || truth(aWithYear) == 1)
                {
                    std::string year;
                    if (!repr_of(aYear, year))
                        return nullptr;
                    text += " " + year;
                }
                return failed() ? nullptr : make_str(centered(text, width));
            }

            builtin_entry const sTextCalendarMethods[] =
            {
                { "prweek", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("prweek", aArgs, aKwargs, { "theweek", "width" }, 2u, 0u, a))
                            return nullptr;
                        std::string text;
                        if (!format_week(aArgs[0], a[0], a[1], text) || !print_text(text, false))
                            return nullptr;
                        return new_reference(none());
                    } },
                { "formatday", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("formatday", aArgs, aKwargs, { "day", "weekday", "width" }, 3u, 0u, a))
                            return nullptr;
                        long long width = 0, day = 0;
                        if (!to_index(a[2], width) || !to_index(a[0], day))
                            return nullptr;
                        std::string text;
                        if (day != 0)
                        {
                            char buffer[32];
                            std::snprintf(buffer, sizeof(buffer), "%2lld", day);
                            text = buffer;
                        }
                        return make_str(centered(text, width));
                    } },
                { "formatweek", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("formatweek", aArgs, aKwargs, { "theweek", "width" }, 2u, 0u, a))
                            return nullptr;
                        std::vector<object*> days;
                        if (!collect(a[0], days))
                            return nullptr;
                        std::string result;
                        bool ok = true;
                        for (std::size_t i = 0u; i < days.size() && ok; ++i)
                        {
                            std::vector<object*> pair;
                            ok = collect(days[i], pair);
                            if (ok && pair.size() != 2u)
                                ok = (error("ValueError", pair.size() > 2u ? "too many values to unpack (expected 2)" : "not enough values to unpack (expected 2, got " + std::to_string(pair.size()) + ")"), false);
                            std::string text;
                            if (ok)
                            {
                                ref const day{ method(aArgs[0], "formatday", { new_reference(pair[0]), new_reference(pair[1]), new_reference(a[1]) }) };
                                ok = str_result(day.get(), text);
                            }
                            release_all(pair);
                            if (i != 0u)
                                result += " ";
                            result += text;
                        }
                        release_all(days);
                        return ok ? make_str(result) : nullptr;
                    } },
                { "formatweekday", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("formatweekday", aArgs, aKwargs, { "day", "width" }, 2u, 0u, a))
                            return nullptr;
                        return text_formatweekday(aArgs[0], a[0], a[1]);
                    } },
                { "formatweekheader", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("formatweekheader", aArgs, aKwargs, { "width" }, 1u, 0u, a))
                            return nullptr;
                        ref const weekdays{ method(aArgs[0], "iterweekdays", {}) };
                        std::vector<object*> days;
                        if (!items_of(weekdays.get(), days))
                            return nullptr;
                        std::string result;
                        bool ok = true;
                        for (std::size_t i = 0u; i < days.size() && ok; ++i)
                        {
                            ref const name{ method(aArgs[0], "formatweekday", { new_reference(days[i]), new_reference(a[0]) }) };
                            std::string text;
                            ok = str_result(name.get(), text);
                            if (i != 0u)
                                result += " ";
                            result += text;
                        }
                        release_all(days);
                        return ok ? make_str(result) : nullptr;
                    } },
                { "formatmonthname", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("formatmonthname", aArgs, aKwargs, { "theyear", "themonth", "width", "withyear" }, 3u, 0u, a))
                            return nullptr;
                        return text_formatmonthname(a[0], a[1], a[2], a[3]);
                    } },
                { "prmonth", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("prmonth", aArgs, aKwargs, { "theyear", "themonth", "w", "l" }, 2u, 0u, a))
                            return nullptr;
                        ref const zero{ make_int(0) };
                        ref const text{ method(aArgs[0], "formatmonth", { new_reference(a[0]), new_reference(a[1]),
                            new_reference(a[2] != nullptr ? a[2] : zero.get()), new_reference(a[3] != nullptr ? a[3] : zero.get()) }) };
                        std::string value;
                        if (!str_result(text.get(), value) || !print_text(value, false))
                            return nullptr;
                        return new_reference(none());
                    } },
                { "formatmonth", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("formatmonth", aArgs, aKwargs, { "theyear", "themonth", "w", "l" }, 2u, 0u, a))
                            return nullptr;
                        auto const self = aArgs[0];
                        long long w = 0, l = 0;
                        if (!int_argument(a[2], 0, w) || !int_argument(a[3], 0, l))
                            return nullptr;
                        w = std::max(2LL, w);
                        l = std::max(1LL, l);
                        auto const newlines = std::string(static_cast<std::size_t>(l), '\n');
                        ref const width{ make_int(w) };
                        ref const name{ method(self, "formatmonthname", { new_reference(a[0]), new_reference(a[1]), make_int(7 * (w + 1) - 1) }) };
                        std::string text, result;
                        if (!str_result(name.get(), text))
                            return nullptr;
                        result = right_stripped(text) + newlines;
                        ref const header{ method(self, "formatweekheader", { new_reference(width.get()) }) };
                        if (!str_result(header.get(), text))
                            return nullptr;
                        result += right_stripped(text) + newlines;
                        ref const weeks{ method(self, "monthdays2calendar", { new_reference(a[0]), new_reference(a[1]) }) };
                        std::vector<object*> rows;
                        if (!items_of(weeks.get(), rows))
                            return nullptr;
                        bool ok = true;
                        for (auto week : rows)
                        {
                            if (!(ok = format_week(self, week, width.get(), text)))
                                break;
                            result += right_stripped(text) + newlines;
                        }
                        release_all(rows);
                        return ok ? make_str(result) : nullptr;
                    } },
                { "formatyear", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("formatyear", aArgs, aKwargs, { "theyear", "w", "l", "c", "m" }, 1u, 0u, a))
                            return nullptr;
                        auto const self = aArgs[0];
                        long long w = 0, l = 0, c = 0, m = 0;
                        if (!int_argument(a[1], 2, w) || !int_argument(a[2], 1, l) || !int_argument(a[3], 6, c) || !int_argument(a[4], 3, m))
                            return nullptr;
                        w = std::max(2LL, w);
                        l = std::max(1LL, l);
                        c = std::max(2LL, c);
                        auto const colwidth = (w + 1) * 7 - 1;
                        auto const newlines = std::string(static_cast<std::size_t>(l), '\n');
                        std::string year;
                        if (!repr_of(a[0], year))
                            return nullptr;
                        std::string result = right_stripped(centered(year, colwidth * m + c * (m - 1))) + newlines;
                        ref const width{ make_int(w) };
                        ref const headerObject{ method(self, "formatweekheader", { new_reference(width.get()) }) };
                        std::string header;
                        if (!str_result(headerObject.get(), header))
                            return nullptr;
                        ref const data{ method(self, "yeardays2calendar", { new_reference(a[0]), make_int(m) }) };
                        std::vector<object*> rows;
                        if (!items_of(data.get(), rows))
                            return nullptr;
                        bool ok = true;
                        for (std::size_t i = 0u; i < rows.size() && ok; ++i)
                        {
                            auto const ii = static_cast<long long>(i);
                            std::vector<std::string> names, headers;
                            for (auto k = m * ii + 1; k < std::min(m * (ii + 1) + 1, 13LL) && ok; ++k)
                            {
                                ref const name{ method(self, "formatmonthname", { new_reference(a[0]), make_int(k), make_int(colwidth), make_bool(false) }) };
                                std::string text;
                                ok = str_result(name.get(), text);
                                names.push_back(text);
                                headers.push_back(header);
                            }
                            std::string line;
                            ok = ok && format_strings(names, colwidth, c, line);
                            if (!ok)
                                break;
                            result += newlines + right_stripped(line) + newlines;
                            ok = format_strings(headers, colwidth, c, line);
                            if (!ok)
                                break;
                            result += right_stripped(line) + newlines;
                            std::vector<object*> calendars;
                            if (!(ok = collect(rows[i], calendars)))
                                break;
                            long long height = 0;
                            std::vector<long long> heights;
                            for (auto cal : calendars)
                            {
                                auto const h = length(cal);
                                if (h < 0)
                                    ok = false;
                                heights.push_back(h);
                                height = std::max(height, h);
                            }
                            if (calendars.empty())
                                ok = (error("ValueError", "max() iterable argument is empty"), false);
                            for (long long j = 0; j < height && ok; ++j)
                            {
                                std::vector<std::string> weeks;
                                for (std::size_t k = 0u; k < calendars.size() && ok; ++k)
                                {
                                    if (j >= heights[k])
                                        weeks.emplace_back();
                                    else
                                    {
                                        ref const index{ make_int(j) };
                                        ref const week{ getitem(calendars[k], index.get()) };
                                        std::string text;
                                        ok = week && format_week(self, week.get(), width.get(), text);
                                        weeks.push_back(text);
                                    }
                                }
                                ok = ok && format_strings(weeks, colwidth, c, line);
                                if (ok)
                                    result += right_stripped(line) + newlines;
                            }
                            release_all(calendars);
                        }
                        release_all(rows);
                        return ok ? make_str(result) : nullptr;
                    } },
                { "pryear", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("pryear", aArgs, aKwargs, { "theyear", "w", "l", "c", "m" }, 1u, 0u, a))
                            return nullptr;
                        ref const text{ method(aArgs[0], "formatyear", { new_reference(a[0]), a[1] != nullptr ? new_reference(a[1]) : make_int(0),
                            a[2] != nullptr ? new_reference(a[2]) : make_int(0), a[3] != nullptr ? new_reference(a[3]) : make_int(6),
                            a[4] != nullptr ? new_reference(a[4]) : make_int(3) }) };
                        std::string value;
                        if (!str_result(text.get(), value) || !print_text(value, false))
                            return nullptr;
                        return new_reference(none());
                    } }
            };

            // --- HTMLCalendar

            bool attribute_text(object* aSelf, char const* aName, std::string& aResult)
            {
                ref const value{ getattr(aSelf, aName) };
                return value && str_of(value.get(), aResult);
            }

            object* html_formatweekday(object* aSelf, object* aDay)
            {
                ref const classes{ getattr(aSelf, "cssclasses_weekday_head") };
                if (!classes)
                    return nullptr;
                ref const cls{ getitem(classes.get(), aDay) };
                ref const name{ cls ? getitem(s.dayAbbr, aDay) : nullptr };
                std::string c, n;
                if (!name || !str_of(cls.get(), c) || !str_of(name.get(), n))
                    return nullptr;
                return make_str("<th class=\"" + c + "\">" + n + "</th>");
            }

            object* html_formatmonthname(object* aSelf, object* aYear, object* aMonth, object* aWithYear)
            {
                long long month = 0;
                if (!validate_month(aMonth, month))
                    return nullptr;
                ref const name{ getitem(s.monthName, aMonth) };
                std::string text, cls;
                if (!name || !str_of(name.get(), text))
                    return nullptr;
                if (aWithYear == nullptr || truth(aWithYear) == 1)
                {
                    std::string year;
                    if (!str_of(aYear, year))
                        return nullptr;
                    text += " " + year;
                }
                if (failed() || !attribute_text(aSelf, "cssclass_month_head", cls))
                    return nullptr;
                return make_str("<tr><th colspan=\"7\" class=\"" + cls + "\">" + text + "</th></tr>");
            }

            builtin_entry const sHtmlCalendarMethods[] =
            {
                { "formatday", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("formatday", aArgs, aKwargs, { "day", "weekday" }, 2u, 0u, a))
                            return nullptr;
                        long long day = 0;
                        if (!to_index(a[0], day))
                            return nullptr;
                        std::string cls;
                        if (day == 0)
                            return attribute_text(aArgs[0], "cssclass_noday", cls) ? make_str("<td class=\"" + cls + "\">&nbsp;</td>") : nullptr;
                        ref const classes{ getattr(aArgs[0], "cssclasses") };
                        ref const c{ classes ? getitem(classes.get(), a[1]) : nullptr };
                        if (!c || !str_of(c.get(), cls))
                            return nullptr;
                        return make_str("<td class=\"" + cls + "\">" + std::to_string(day) + "</td>");
                    } },
                { "formatweek", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("formatweek", aArgs, aKwargs, { "theweek" }, 1u, 0u, a))
                            return nullptr;
                        std::vector<object*> days;
                        if (!collect(a[0], days))
                            return nullptr;
                        std::string result;
                        bool ok = true;
                        for (std::size_t i = 0u; i < days.size() && ok; ++i)
                        {
                            std::vector<object*> pair;
                            ok = collect(days[i], pair);
                            if (ok && pair.size() != 2u)
                                ok = (error("ValueError", pair.size() > 2u ? "too many values to unpack (expected 2)" : "not enough values to unpack (expected 2, got " + std::to_string(pair.size()) + ")"), false);
                            if (ok)
                            {
                                ref const day{ method(aArgs[0], "formatday", { new_reference(pair[0]), new_reference(pair[1]) }) };
                                std::string text;
                                ok = str_result(day.get(), text);
                                result += text;
                            }
                            release_all(pair);
                        }
                        release_all(days);
                        return ok ? make_str("<tr>" + result + "</tr>") : nullptr;
                    } },
                { "formatweekday", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("formatweekday", aArgs, aKwargs, { "day" }, 1u, 0u, a))
                            return nullptr;
                        return html_formatweekday(aArgs[0], a[0]);
                    } },
                { "formatweekheader", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("formatweekheader", aArgs, aKwargs, {}, 0u, 0u, a))
                            return nullptr;
                        ref const weekdays{ method(aArgs[0], "iterweekdays", {}) };
                        std::vector<object*> days;
                        if (!items_of(weekdays.get(), days))
                            return nullptr;
                        std::string result;
                        bool ok = true;
                        for (std::size_t i = 0u; i < days.size() && ok; ++i)
                        {
                            ref const name{ method(aArgs[0], "formatweekday", { new_reference(days[i]) }) };
                            std::string text;
                            ok = str_result(name.get(), text);
                            result += text;
                        }
                        release_all(days);
                        return ok ? make_str("<tr>" + result + "</tr>") : nullptr;
                    } },
                { "formatmonthname", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("formatmonthname", aArgs, aKwargs, { "theyear", "themonth", "withyear" }, 2u, 0u, a))
                            return nullptr;
                        return html_formatmonthname(aArgs[0], a[0], a[1], a[2]);
                    } },
                { "formatmonth", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("formatmonth", aArgs, aKwargs, { "theyear", "themonth", "withyear" }, 2u, 0u, a))
                            return nullptr;
                        auto const self = aArgs[0];
                        std::string cls, text;
                        if (!attribute_text(self, "cssclass_month", cls))
                            return nullptr;
                        std::string result = "<table border=\"0\" cellpadding=\"0\" cellspacing=\"0\" class=\"" + cls + "\">\n";
                        ref const withYear{ a[2] != nullptr ? new_reference(a[2]) : make_bool(true) };
                        ref const kw{ keywords({ { "withyear", withYear.get() } }) };
                        ref const nameMethod{ getattr(self, "formatmonthname") };
                        ref const name{ nameMethod ? invoke(nameMethod.get(), { new_reference(a[0]), new_reference(a[1]) }, kw.get()) : nullptr };
                        if (!str_result(name.get(), text))
                            return nullptr;
                        result += text + "\n";
                        ref const header{ method(self, "formatweekheader", {}) };
                        if (!str_result(header.get(), text))
                            return nullptr;
                        result += text + "\n";
                        ref const weeks{ method(self, "monthdays2calendar", { new_reference(a[0]), new_reference(a[1]) }) };
                        std::vector<object*> rows;
                        if (!items_of(weeks.get(), rows))
                            return nullptr;
                        bool ok = true;
                        for (auto week : rows)
                        {
                            ref const row{ method(self, "formatweek", { new_reference(week) }) };
                            if (!(ok = str_result(row.get(), text)))
                                break;
                            result += text + "\n";
                        }
                        release_all(rows);
                        return ok ? make_str(result + "</table>\n") : nullptr;
                    } },
                { "formatyear", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("formatyear", aArgs, aKwargs, { "theyear", "width" }, 1u, 0u, a))
                            return nullptr;
                        auto const self = aArgs[0];
                        long long width = 0;
                        if (!int_argument(a[1], 3, width))
                            return nullptr;
                        width = std::max(width, 1LL);
                        std::string cls, head, year;
                        if (!attribute_text(self, "cssclass_year", cls) || !attribute_text(self, "cssclass_year_head", head) || !str_of(a[0], year))
                            return nullptr;
                        std::string result = "<table border=\"0\" cellpadding=\"0\" cellspacing=\"0\" class=\"" + cls + "\">\n";
                        result += "<tr><th colspan=\"" + std::to_string(width) + "\" class=\"" + head + "\">" + year + "</th></tr>";
                        ref const no{ make_bool(false) };
                        ref const kw{ keywords({ { "withyear", no.get() } }) };
                        ref const monthMethod{ getattr(self, "formatmonth") };
                        if (!monthMethod)
                            return nullptr;
                        for (long long i = 1; i < 13; i += width)
                        {
                            result += "<tr>";
                            for (auto m = i; m < std::min(i + width, 13LL); ++m)
                            {
                                ref const month{ invoke(monthMethod.get(), { new_reference(a[0]), make_int(m) }, kw.get()) };
                                std::string text;
                                if (!str_result(month.get(), text))
                                    return nullptr;
                                result += "<td>" + text + "</td>";
                            }
                            result += "</tr>";
                        }
                        return make_str(result + "</table>");
                    } },
                { "formatyearpage", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("formatyearpage", aArgs, aKwargs, { "theyear", "width", "css", "encoding" }, 1u, 0u, a))
                            return nullptr;
                        std::string encoding = "utf-8";
                        if (a[3] != nullptr && kind_of(a[3]) != NEOS_PY_NONE && !str_of(a[3], encoding))
                            return nullptr;
                        std::string result = "<?xml version=\"1.0\" encoding=\"" + encoding + "\"?>\n";
                        result += "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Strict//EN\" \"http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd\">\n";
                        result += "<html>\n<head>\n<meta http-equiv=\"Content-Type\" content=\"text/html; charset=" + encoding + "\" />\n";
                        if (a[2] == nullptr || kind_of(a[2]) != NEOS_PY_NONE)
                        {
                            std::string css = "calendar.css";
                            if (a[2] != nullptr && !str_of(a[2], css))
                                return nullptr;
                            result += "<link rel=\"stylesheet\" type=\"text/css\" href=\"" + css + "\" />\n";
                        }
                        long long year = 0;
                        if (!to_index(a[0], year))
                            return nullptr;
                        result += "<title>Calendar for " + std::to_string(year) + "</title>\n</head>\n<body>\n";
                        ref const body{ method(aArgs[0], "formatyear", { new_reference(a[0]), a[1] != nullptr ? new_reference(a[1]) : make_int(3) }) };
                        std::string text;
                        if (!str_result(body.get(), text))
                            return nullptr;
                        result += text + "</body>\n</html>\n";
                        ref const page{ make_str(result) };
                        return method(page.get(), "encode", { make_str(encoding), make_str("xmlcharrefreplace") });
                    } }
            };

            // --- different_locale, and the Locale calendars

            object* set_time_locale(object* aLocale)   // (locale.setlocale(locale.LC_TIME, aLocale): the result)
            {
                ref const setlocale{ module_attribute("locale", "setlocale") };
                ref const category{ module_attribute("locale", "LC_TIME") };
                if (!setlocale || !category)
                    return nullptr;
                return invoke(setlocale.get(), { new_reference(category.get()), new_reference(aLocale) });
            }

            builtin_entry const sDifferentLocaleMethods[] =
            {
                { "__init__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("different_locale.__init__", aArgs, aKwargs, { "locale" }, 1u, 0u, a))
                            return nullptr;
                        if (setattr(aArgs[0], "locale", a[0]) < 0 || setattr(aArgs[0], "oldlocale", none()) < 0)
                            return nullptr;
                        return new_reference(none());
                    } },
                { "__enter__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        ref const old{ set_time_locale(none()) };
                        if (!old || setattr(aArgs[0], "oldlocale", old.get()) < 0)
                            return nullptr;
                        ref const locale{ getattr(aArgs[0], "locale") };
                        ref const result{ locale ? set_time_locale(locale.get()) : nullptr };
                        return result ? new_reference(none()) : nullptr;
                    } },
                { "__exit__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        ref const old{ getattr(aArgs[0], "oldlocale") };
                        ref const result{ old ? set_time_locale(old.get()) : nullptr };
                        return result ? new_reference(none()) : nullptr;
                    } }
            };

            // (with different_locale(aLocale): aAction())
            template <typename Action>
            object* in_locale(object* aLocale, Action aAction)
            {
                ref const old{ set_time_locale(none()) };
                if (!old)
                    return nullptr;
                ref const changed{ set_time_locale(aLocale) };
                if (!changed)
                    return nullptr;
                ref result{ aAction() };
                if (!result)
                {
                    ref const e{ take_exception() };
                    ref const restored{ set_time_locale(old.get()) };
                    clear_error();
                    raise_exception(e.get());
                    return nullptr;
                }
                ref const restored{ set_time_locale(old.get()) };
                return restored ? new_reference(result.get()) : nullptr;
            }

            object* default_locale()    // (_get_default_locale())
            {
                ref locale{ set_time_locale(none()) };
                if (!locale)
                    return nullptr;
                if (auto const text = str_of_value(locale.get()); text != nullptr && *text == "C")
                {
                    ref const empty{ make_str("") };
                    return in_locale(empty.get(), [] { return set_time_locale(none()); });
                }
                return new_reference(locale.get());
            }

            object* locale_init(char const* aName, std::vector<object*> const& aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                if (!self_arguments(aName, aArgs, aKwargs, { "firstweekday", "locale" }, 0u, 0u, a))
                    return nullptr;
                ref const first{ a[0] != nullptr ? new_reference(a[0]) : make_int(0) };
                if (setattr(aArgs[0], "firstweekday", first.get()) < 0)
                    return nullptr;
                ref const locale{ a[1] != nullptr && kind_of(a[1]) != NEOS_PY_NONE ? new_reference(a[1]) : default_locale() };
                if (!locale || setattr(aArgs[0], "locale", locale.get()) < 0)
                    return nullptr;
                return new_reference(none());
            }

            builtin_entry const sLocaleTextCalendarMethods[] =
            {
                { "__init__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return locale_init("LocaleTextCalendar.__init__", aArgs, aKwargs); } },
                { "formatweekday", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("formatweekday", aArgs, aKwargs, { "day", "width" }, 2u, 0u, a))
                            return nullptr;
                        ref const locale{ getattr(aArgs[0], "locale") };
                        return locale ? in_locale(locale.get(), [&] { return text_formatweekday(aArgs[0], a[0], a[1]); }) : nullptr;
                    } },
                { "formatmonthname", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("formatmonthname", aArgs, aKwargs, { "theyear", "themonth", "width", "withyear" }, 3u, 0u, a))
                            return nullptr;
                        ref const locale{ getattr(aArgs[0], "locale") };
                        return locale ? in_locale(locale.get(), [&] { return text_formatmonthname(a[0], a[1], a[2], a[3]); }) : nullptr;
                    } }
            };

            builtin_entry const sLocaleHtmlCalendarMethods[] =
            {
                { "__init__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return locale_init("LocaleHTMLCalendar.__init__", aArgs, aKwargs); } },
                { "formatweekday", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("formatweekday", aArgs, aKwargs, { "day" }, 1u, 0u, a))
                            return nullptr;
                        ref const locale{ getattr(aArgs[0], "locale") };
                        return locale ? in_locale(locale.get(), [&] { return html_formatweekday(aArgs[0], a[0]); }) : nullptr;
                    } },
                { "formatmonthname", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("formatmonthname", aArgs, aKwargs, { "theyear", "themonth", "withyear" }, 2u, 0u, a))
                            return nullptr;
                        ref const locale{ getattr(aArgs[0], "locale") };
                        return locale ? in_locale(locale.get(), [&] { return html_formatmonthname(aArgs[0], a[0], a[1], a[2]); }) : nullptr;
                    } }
            };

            // --- the module's functions

            object* default_calendar()  // (calendar.c)
            {
                return module_attribute("calendar", "c");
            }

            builtin_entry const sCalendarFunctions[] =
            {
                { "isleap", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long year = 0;
                        if (!parse_arguments("isleap", aArgs, aKwargs, { "year" }, 1u, 0u, a) || !to_index(a[0], year))
                            return nullptr;
                        return make_bool(is_leap(year));
                    } },
                { "leapdays", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long y1 = 0, y2 = 0;
                        if (!parse_arguments("leapdays", aArgs, aKwargs, { "y1", "y2" }, 2u, 0u, a) || !to_index(a[0], y1) || !to_index(a[1], y2))
                            return nullptr;
                        --y1;
                        --y2;
                        return make_int((floor_div(y2, 4) - floor_div(y1, 4)) - (floor_div(y2, 100) - floor_div(y1, 100)) + (floor_div(y2, 400) - floor_div(y1, 400)));
                    } },
                { "weekday", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("weekday", aArgs, aKwargs, { "year", "month", "day" }, 3u, 0u, a))
                            return nullptr;
                        return weekday_of(a[0], a[1], a[2]);
                    } },
                { "monthrange", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("monthrange", aArgs, aKwargs, { "year", "month" }, 2u, 0u, a))
                            return nullptr;
                        ref first;
                        long long days = 0;
                        if (!month_range(a[0], a[1], first, days))
                            return nullptr;
                        return make_tuple({ new_reference(first.get()), make_int(days) });
                    } },
                { "setfirstweekday", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("setfirstweekday", aArgs, aKwargs, { "firstweekday" }, 1u, 0u, a))
                            return nullptr;
                        ref const zero{ make_int(0) };
                        ref const six{ make_int(6) };
                        ref const low{ compare(NEOS_PY_LE, zero.get(), a[0]) };
                        if (!low)
                            return nullptr;
                        int inRange = truth(low.get());
                        if (inRange == 1)
                        {
                            ref const high{ compare(NEOS_PY_LE, a[0], six.get()) };
                            if (!high)
                                return nullptr;
                            inRange = truth(high.get());
                        }
                        if (inRange < 0)
                            return nullptr;
                        if (inRange == 0)
                        {
                            ref const cls{ module_attribute("calendar", "IllegalWeekdayError") };
                            if (cls)
                                if (ref const e{ invoke(cls.get(), { new_reference(a[0]) }) })
                                    fn_raise(e.get());
                            return nullptr;
                        }
                        ref const c{ default_calendar() };
                        return c && setattr(c.get(), "firstweekday", a[0]) >= 0 ? new_reference(none()) : nullptr;
                    } },
                { "format", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long width = 0, spacing = 0;
                        if (!parse_arguments("format", aArgs, aKwargs, { "cols", "colwidth", "spacing" }, 1u, 0u, a) || !int_argument(a[1], 20, width) ||
                            !int_argument(a[2], 6, spacing))
                            return nullptr;
                        std::string text;
                        if (!format_columns(a[0], width, spacing, text) || !print_text(text, true))
                            return nullptr;
                        return new_reference(none());
                    } },
                { "formatstring", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long width = 0, spacing = 0;
                        if (!parse_arguments("formatstring", aArgs, aKwargs, { "cols", "colwidth", "spacing" }, 1u, 0u, a) || !int_argument(a[1], 20, width) ||
                            !int_argument(a[2], 6, spacing))
                            return nullptr;
                        std::string text;
                        return format_columns(a[0], width, spacing, text) ? make_str(text) : nullptr;
                    } },
                { "timegm", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("timegm", aArgs, aKwargs, { "tuple" }, 1u, 0u, a))
                            return nullptr;
                        std::vector<object*> fields;
                        {
                            ref const slice{ fn_builtin("slice") };
                            ref const six{ slice ? invoke(slice.get(), { make_int(6) }) : nullptr };
                            ref const head{ six ? getitem(a[0], six.get()) : nullptr };
                            if (!head || !collect(head.get(), fields))
                                return nullptr;
                        }
                        if (fields.size() != 6u)
                        {
                            auto const count = fields.size();
                            release_all(fields);
                            return error("ValueError", count > 6u ? "too many values to unpack (expected 6)" : "not enough values to unpack (expected 6, got " + std::to_string(count) + ")");
                        }
                        ref const one{ make_int(1) };
                        ref const date{ date_of(fields[0], fields[1], one.get()) };
                        ref const ordinal{ date ? method(date.get(), "toordinal", {}) : nullptr };
                        auto const step = [](object* aLhs, int aOperation, object* aRhs) { return ref{ aLhs != nullptr ? binary(aOperation, aLhs, aRhs, false) : nullptr }; };
                        ref const epoch{ make_int(719163) };
                        ref const twentyFour{ make_int(24) };
                        ref const sixty{ make_int(60) };
                        ref days{ step(ordinal.get(), NEOS_PY_SUBTRACT, epoch.get()) };
                        days = step(days.get(), NEOS_PY_ADD, fields[2]);
                        days = step(days.get(), NEOS_PY_SUBTRACT, one.get());
                        ref hours{ step(days.get(), NEOS_PY_MULTIPLY, twentyFour.get()) };
                        hours = step(hours.get(), NEOS_PY_ADD, fields[3]);
                        ref minutes{ step(hours.get(), NEOS_PY_MULTIPLY, sixty.get()) };
                        minutes = step(minutes.get(), NEOS_PY_ADD, fields[4]);
                        ref seconds{ step(minutes.get(), NEOS_PY_MULTIPLY, sixty.get()) };
                        seconds = step(seconds.get(), NEOS_PY_ADD, fields[5]);
                        release_all(fields);
                        return seconds ? new_reference(seconds.get()) : nullptr;
                    } }
            };

            bool calendar_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                ref const intEnum{ module_attribute("enum", "IntEnum") };
                ref const globalEnum{ module_attribute("enum", "global_enum") };
                if (!intEnum || !globalEnum)
                    return false;
                ref const moduleKw{ keywords({ { "module", ref{ make_str("calendar") }.get() } }) };
                static char const* const sMonths[] = { "JANUARY", "FEBRUARY", "MARCH", "APRIL", "MAY", "JUNE", "JULY", "AUGUST", "SEPTEMBER", "OCTOBER",
                    "NOVEMBER", "DECEMBER" };
                static char const* const sDays[] = { "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY", "SUNDAY" };
                auto const make_enum = [&](char const* aName, char const* const* aNames, int aCount, int aStart) -> object*
                {
                    std::vector<object*> pairs;
                    for (int i = 0; i < aCount; ++i)
                        pairs.push_back(make_tuple({ make_str(aNames[i]), make_int(aStart + i) }));
                    ref const args{ make_tuple({ make_str(aName), make_list(std::move(pairs)) }) };
                    ref const cls{ call(intEnum.get(), args.get(), moduleKw.get()) };
                    return cls ? invoke(globalEnum.get(), { new_reference(cls.get()) }) : nullptr;
                };
                if (s.month == nullptr)
                {
                    s.month = make_enum("Month", sMonths, 12, 1);
                    s.day = make_enum("Day", sDays, 7, 0);
                    if (s.month == nullptr || s.day == nullptr)
                        return false;
                    ref const localizedMonth{ make_class_of("calendar", "_localized_month", {}, sLocalizedMonthMethods) };
                    ref const localizedDay{ make_class_of("calendar", "_localized_day", {}, sLocalizedDayMethods) };
                    if (!localizedMonth || !localizedDay)
                        return false;
                    s.dayName = invoke(localizedDay.get(), { make_str("%A") });
                    s.dayAbbr = invoke(localizedDay.get(), { make_str("%a") });
                    s.monthName = invoke(localizedMonth.get(), { make_str("%B") });
                    s.monthAbbr = invoke(localizedMonth.get(), { make_str("%b") });
                    if (!s.dayName || !s.dayAbbr || !s.monthName || !s.monthAbbr)
                        return false;
                    // (Calendar's firstweekday: property(getfirstweekday, setfirstweekday))
                    ref const base{ make_class_of("calendar", "Calendar", {}, sCalendarMethods) };
                    if (!base)
                        return false;
                    ref const getter{ getattr(base.get(), "getfirstweekday") };
                    ref const setter{ getattr(base.get(), "setfirstweekday") };
                    ref const property{ getter && setter ? b_property({ getter.get(), setter.get() }, nullptr) : nullptr };
                    if (!property || class_setattr_raw(base.get(), "firstweekday", property.get()) < 0)
                        return false;
                    s.textCalendar = make_class_of("calendar", "TextCalendar", { base.get() }, sTextCalendarMethods);
                    std::vector<std::pair<std::string, object*>> css;
                    {
                        std::vector<object*> classes;
                        for (auto n : { "mon", "tue", "wed", "thu", "fri", "sat", "sun" })
                            classes.push_back(make_str(n));
                        ref const list{ make_list(std::move(classes)) };
                        css = { { "cssclasses", new_reference(list.get()) }, { "cssclasses_weekday_head", new_reference(list.get()) },
                            { "cssclass_noday", make_str("noday") }, { "cssclass_month_head", make_str("month") }, { "cssclass_month", make_str("month") },
                            { "cssclass_year_head", make_str("year") }, { "cssclass_year", make_str("year") } };
                    }
                    s.htmlCalendar = make_class_of("calendar", "HTMLCalendar", { base.get() }, sHtmlCalendarMethods, std::move(css));
                    s.differentLocale = make_class_of("calendar", "different_locale", {}, sDifferentLocaleMethods);
                    if (!s.textCalendar || !s.htmlCalendar || !s.differentLocale)
                        return false;
                    aAttributes.emplace_back("Calendar", new_reference(base.get()));
                }
                else
                    aAttributes.emplace_back("Calendar", getattr(s.textCalendar, "__base__"));
                ref const valueError{ exception_class("ValueError") };
                ref const indexError{ exception_class("IndexError") };
                ref const illegalMonth{ make_class_of("calendar", "IllegalMonthError", { valueError.get(), indexError.get() }, sIllegalMonthMethods) };
                ref const illegalWeekday{ make_class_of("calendar", "IllegalWeekdayError", { valueError.get() }, sIllegalWeekdayMethods) };
                ref const localeText{ make_class_of("calendar", "LocaleTextCalendar", { s.textCalendar }, sLocaleTextCalendarMethods) };
                ref const localeHtml{ make_class_of("calendar", "LocaleHTMLCalendar", { s.htmlCalendar }, sLocaleHtmlCalendarMethods) };
                ref const c{ call(s.textCalendar, nullptr, nullptr) };
                if (!illegalMonth || !illegalWeekday || !localeText || !localeHtml || !c)
                    return false;
                aAttributes.emplace_back("error", new_reference(valueError.get()));
                aAttributes.emplace_back("IllegalMonthError", new_reference(illegalMonth.get()));
                aAttributes.emplace_back("IllegalWeekdayError", new_reference(illegalWeekday.get()));
                aAttributes.emplace_back("Month", new_reference(s.month));
                aAttributes.emplace_back("Day", new_reference(s.day));
                for (auto n : sMonths)
                    aAttributes.emplace_back(n, getattr(s.month, n));
                for (auto n : sDays)
                    aAttributes.emplace_back(n, getattr(s.day, n));
                {
                    std::vector<object*> days;
                    for (auto d : { 0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 })
                        days.push_back(make_int(d));
                    aAttributes.emplace_back("mdays", make_list(std::move(days)));
                }
                aAttributes.emplace_back("_localized_month", getattr(s.monthName, "__class__"));
                aAttributes.emplace_back("_localized_day", getattr(s.dayName, "__class__"));
                aAttributes.emplace_back("day_name", new_reference(s.dayName));
                aAttributes.emplace_back("day_abbr", new_reference(s.dayAbbr));
                aAttributes.emplace_back("month_name", new_reference(s.monthName));
                aAttributes.emplace_back("month_abbr", new_reference(s.monthAbbr));
                aAttributes.emplace_back("TextCalendar", new_reference(s.textCalendar));
                aAttributes.emplace_back("HTMLCalendar", new_reference(s.htmlCalendar));
                aAttributes.emplace_back("different_locale", new_reference(s.differentLocale));
                aAttributes.emplace_back("LocaleTextCalendar", new_reference(localeText.get()));
                aAttributes.emplace_back("LocaleHTMLCalendar", new_reference(localeHtml.get()));
                for (auto const& f : sCalendarFunctions)
                    aAttributes.emplace_back(f.name, make_builtin(f));
                aAttributes.emplace_back("c", new_reference(c.get()));
                for (auto const& [name, attribute] : { std::pair{ "firstweekday", "getfirstweekday" }, std::pair{ "monthcalendar", "monthdayscalendar" },
                    std::pair{ "prweek", "prweek" }, std::pair{ "week", "formatweek" }, std::pair{ "weekheader", "formatweekheader" },
                    std::pair{ "prmonth", "prmonth" }, std::pair{ "month", "formatmonth" }, std::pair{ "calendar", "formatyear" }, std::pair{ "prcal", "pryear" } })
                {
                    auto const bound = getattr(c.get(), attribute);
                    if (bound == nullptr)
                        return false;
                    aAttributes.emplace_back(name, bound);
                }
                aAttributes.emplace_back("_colwidth", make_int(20));
                aAttributes.emplace_back("_spacing", make_int(6));
                aAttributes.emplace_back("EPOCH", make_int(1970));
                aAttributes.emplace_back("_EPOCH_ORD", make_int(719163));
                std::vector<object*> all;
                for (auto n : { "IllegalMonthError", "IllegalWeekdayError", "setfirstweekday", "firstweekday", "isleap", "leapdays", "weekday", "monthrange",
                    "monthcalendar", "prmonth", "month", "prcal", "calendar", "timegm", "month_name", "month_abbr", "day_name", "day_abbr", "Calendar",
                    "TextCalendar", "HTMLCalendar", "LocaleTextCalendar", "LocaleHTMLCalendar", "weekheader", "Day", "Month", "JANUARY", "FEBRUARY", "MARCH",
                    "APRIL", "MAY", "JUNE", "JULY", "AUGUST", "SEPTEMBER", "OCTOBER", "NOVEMBER", "DECEMBER", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY",
                    "FRIDAY", "SATURDAY", "SUNDAY" })
                    all.push_back(make_str(n));
                aAttributes.emplace_back("__all__", make_list(std::move(all)));
                return true;
            }

            inline bool const sCalendar = register_module("calendar", calendar_module);
        }
    }
}
