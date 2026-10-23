/*
  difflib.cpp

  Copyright (c) 2026 Leigh Johnston.  All Rights Reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are
  met:

     * Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.

     * Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.

     * Neither the name of the copyright holder nor the names of its
       contributors may be used to endorse or promote products derived from
       this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
  IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
  THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
  PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

  Portions of this file are derived from CPython 3.13 (Lib/difflib.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module difflib (CPython 3.13's Lib/difflib.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "difflib_docs.hpp"

namespace neos::python_runtime::detail::difflib_module
{
    namespace
    {
        using pyclass::S;
        using pyclass::I;
        using pyclass::F;
        using pyclass::B;
        using pyclass::Mod;
        using pyclass::none;
        using pyclass::boolean;
        using pyclass::M;
        using pyclass::has_attr;
        using pyclass::get_attr;
        using pyclass::callable;
        using pyclass::module_writer;
        using pyclass::namespace_type;
        using pyclass::try_except;
        using pyclass::try_finally;
        using pyclass::with_;
        using pyclass::unpack2;
        using pyclass::format;
        using pyclass::handler_scope;
        using pyclass::matches;
        using pyclass::iterate;
        using pyclass::unpack;
        using pyclass::call_ex;
        using pyclass::dict_of;
        using pyclass::set_of;
        using pyclass::inplace;
        using pyclass::del_attr;
        using pyclass::del_item;
        using pyclass::formatted;
        using pyclass::yield_from;
        using pyclass::truth_of;
        using pyclass::module_of;
        // (the module's objects (made once, and never destroyed: they outlive the program's objects))
        struct state
        {
            std::vector<std::pair<std::string, py>> attributes;     ///< (the module's attributes, in order)
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "difflib.name"))
            bool made = false;
        };
        state& the_state()
        {
            static state* sState = new state{};
            return *sState;
        }

        // (its entry: the object, as it is defined (or assigned) now)
        py const& object_entry(char const* aQualifiedName)
        {
            auto& s = the_state();
            auto const o = s.objects.find(aQualifiedName);
            if (o == s.objects.end())
                py_raise("SystemError", std::string{ "difflib: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"difflib.name">()))
        template <pyclass::fixed_string Name>
        py const& G() { static py const* const sEntry = &object_entry(Name.value); return *sEntry; }
        // (an object made: one of the module's attributes)
        void define(char const* aModule, char const* aName, py const& aValue)
        {
            auto& s = the_state();
            s.objects[std::string{ aModule } + "." + aName] = aValue;
            for (auto& a : s.attributes)
                if (a.first == aName)
                {
                    a.second = aValue;
                    return;
                }
            s.attributes.emplace_back(aName, aValue);
        }
        // (del name: no longer one of the module's attributes (its object remains, for the module's functions))
        [[maybe_unused]] void undefine(char const* aModule, char const* aName)
        {
            auto& s = the_state();
            (void)aModule;
            std::erase_if(s.attributes, [&](auto const& a) { return a.first == aName; });
        }
        // (a module's global assigned (global name; name = value): its object, and the module's attribute (if it has been imported))
        void set_global(char const* aModule, char const* aName, py const& aValue)
        {
            define(aModule, aName, aValue);
            if (modules_imported(aModule))
                py{ fn_import(aModule) }.set_attr(aName, aValue);
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "difflib", difflib_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- difflib (Lib/difflib.py)

        void make_difflib()
        {
            static module_writer const& w = writer("difflib");
            define("difflib", "__all__", py_list({ S<"get_close_matches">(), S<"ndiff">(), S<"restore">(), S<"SequenceMatcher">(), S<"Differ">(), S<"IS_CHARACTER_JUNK">(), S<"IS_LINE_JUNK">(), S<"context_diff">(), S<"unified_diff">(), S<"diff_bytes">(), S<"HtmlDiff">(), S<"Match">() }));
            define("difflib", "Match", (Mod<"collections">().attr("namedtuple"))(S<"Match">(), S<"a b size">()));
            define("difflib", "_calculate_ratio", w.function("_calculate_ratio", "_calculate_ratio", { py_positional("matches"), py_positional("length") }, [](arguments a) -> py
                {
                    py matches_ = a[0];
                    py length = a[1];
                    if (length.truthy())
                    {
                        return ((F<2.0>()) * matches_) / length;
                    }
                    return F<1.0>();
                }));

                // For each element x in b, set b2j[x] to a list of the indices in
                // b where x appears; the indices are in increasing order; note that
                // the number of times x appears in b is len(b2j[x]) ...
                // when self.isjunk is defined, junk elements don't show up in this
                // map at all, which stops the central find_longest_match method
                // from starting any matching block at a junk element ...
                // b2j also does not contain entries for "popular" elements, meaning
                // elements that account for more than 1 + 1% of the total elements, and
                // when the sequence is reasonably large (>= 200 elements); this can
                // be viewed as an adaptive notion of semi-junk, and yields an enormous
                // speedup when, e.g., comparing program files with hundreds of
                // instances of "return NULL;" ...
                // note that this is only called when b changes; so for cross-product
                // kinds of matches, it's best to call set_seq2 once, then set_seq1
                // repeatedly
            define("difflib", "SequenceMatcher", w.make_class("SequenceMatcher", {  }, {
                { "__init__", w.method("SequenceMatcher", "__init__", { py_positional("isjunk", none()), py_positional("a", S<"">()), py_positional("b", S<"">()), py_positional("autojunk", boolean(true)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py isjunk = a[1];
                        py a_ = a[2];
                        py b = a[3];
                        py autojunk = a[4];
                        // Members:
                        // a
                        // first sequence
                        // b
                        // second sequence; differences are computed as "what do
                        // we need to do to 'a' to change it into 'b'?"
                        // b2j
                        // for x in b, b2j[x] is a list of the indices (into b)
                        // at which x appears; junk and popular elements do not appear
                        // fullbcount
                        // for x in b, fullbcount[x] == the number of times x
                        // appears in b; only materialized if really needed (used
                        // only for computing quick_ratio())
                        // matching_blocks
                        // a list of (i, j, k) triples, where a[i:i+k] == b[j:j+k];
                        // ascending & non-overlapping in i and in j; terminated by
                        // a dummy (len(a), len(b), 0) sentinel
                        // opcodes
                        // a list of (tag, i1, i2, j1, j2) tuples, where tag is
                        // one of
                        // 'replace'   a[i1:i2] should be replaced by b[j1:j2]
                        // 'delete'    a[i1:i2] should be deleted
                        // 'insert'    b[j1:j2] should be inserted
                        // 'equal'     a[i1:i2] == b[j1:j2]
                        // isjunk
                        // a user-supplied function taking a sequence element and
                        // returning true iff the element is "junk" -- this has
                        // subtle but helpful effects on the algorithm, which I'll
                        // get around to writing up someday <0.9 wink>.
                        // DON'T USE!  Only __chain_b uses this.  Use "in self.bjunk".
                        // bjunk
                        // the items in b for which isjunk is True.
                        // bpopular
                        // nonjunk items in b treated as junk by the heuristic (if used).
                        self.set_attr("isjunk", isjunk);
                        py const value1 = none();
                        self.set_attr("a", value1);
                        self.set_attr("b", value1);
                        self.set_attr("autojunk", autojunk);
                        self.method("set_seqs", { a_, b });
                        return none();
                    }) },
                { "set_seqs", w.method("SequenceMatcher", "set_seqs", { py_positional("a"), py_positional("b") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py a_ = a[1];
                        py b = a[2];
                        self.method("set_seq1", { a_ });
                        self.method("set_seq2", { b });
                        return none();
                    }) },
                { "set_seq1", w.method("SequenceMatcher", "set_seq1", { py_positional("a") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py a_ = a[1];
                        if (a_.is(self.attr("a")))
                        {
                            return none();
                        }
                        self.set_attr("a", a_);
                        py const value2 = none();
                        self.set_attr("matching_blocks", value2);
                        self.set_attr("opcodes", value2);
                        return none();
                    }) },
                { "set_seq2", w.method("SequenceMatcher", "set_seq2", { py_positional("b") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py b = a[1];
                        if (b.is(self.attr("b")))
                        {
                            return none();
                        }
                        self.set_attr("b", b);
                        py const value3 = none();
                        self.set_attr("matching_blocks", value3);
                        self.set_attr("opcodes", value3);
                        self.set_attr("fullbcount", none());
                        self.method("_SequenceMatcher__chain_b");
                        return none();
                    }) },
                { "_SequenceMatcher__chain_b", w.method("SequenceMatcher", "__chain_b", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py b;
                        py b2j;
                        py elt;
                        py i;
                        py idxs;
                        py indices;
                        py isjunk;
                        py junk;
                        py n;
                        py ntest;
                        py popular;
                        b = self.attr("b");
                        py const value4 = py_dict();
                        self.set_attr("b2j", value4);
                        b2j = value4;
                        for (py const& item : iterate{ (B<"enumerate">())(b) })
                        {
                            auto const items6 = unpack(item, 2);
                            i = items6[0];
                            elt = items6[1];
                            indices = b2j.method("setdefault", { elt, py_list() });
                            indices.method("append", { i });
                        }
                        // Purge junk elements
                        py const value7 = (B<"set">())();
                        self.set_attr("bjunk", value7);
                        junk = value7;
                        isjunk = self.attr("isjunk");
                        if (isjunk.truthy())
                        {
                            for (py const& item : iterate{ b2j.method("keys") })
                            {
                                elt = item;
                                if (isjunk(elt).truthy())
                                {
                                    junk.method("add", { elt });
                                }
                            }
                            for (py const& item : iterate{ junk })
                            {
                                elt = item;
                                del_item(b2j, elt);
                            }
                        }
                        // Purge popular elements that are not junk
                        py const value10 = (B<"set">())();
                        self.set_attr("bpopular", value10);
                        popular = value10;
                        n = py{ b.size() };
                        if ((self.attr("autojunk").truthy() && (n >= (I<200>()))))
                        {
                            ntest = floordiv(n, (I<100>())) + (I<1>());
                            for (py const& item : iterate{ b2j.method("items") })
                            {
                                auto const items12 = unpack(item, 2);
                                elt = items12[0];
                                idxs = items12[1];
                                if (py{ idxs.size() } > ntest)
                                {
                                    popular.method("add", { elt });
                                }
                            }
                            for (py const& item : iterate{ popular })
                            {
                                elt = item;
                                del_item(b2j, elt);
                            }
                        }
                        return none();
                    }) },
                { "find_longest_match", w.method("SequenceMatcher", "find_longest_match", { py_positional("alo", I<0>()), py_positional("ahi", none()), py_positional("blo", I<0>()), py_positional("bhi", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py alo = a[1];
                        py ahi = a[2];
                        py blo = a[3];
                        py bhi = a[4];
                        py a_;
                        py b;
                        py b2j;
                        py besti;
                        py bestj;
                        py bestsize;
                        py i;
                        py isbjunk;
                        py j;
                        py j2len;
                        py j2lenget;
                        py k;
                        py newj2len;
                        py nothing;
                        // CAUTION:  stripping common prefix or suffix would be incorrect.
                        // E.g.,
                        // ab
                        // acab
                        // Longest matching block is "ab", but if common prefix is
                        // stripped, it's "a" (tied with "b").  UNIX(tm) diff does so
                        // strip, so ends up claiming that ab is changed to acab by
                        // inserting "ca" in the middle.  That's minimal but unintuitive:
                        // "it's obvious" that someone inserted "ac" at the front.
                        // Windiff ends up at the same place as diff, but by pairing up
                        // the unique 'b's and then matching the first two 'a's.
                        auto const items14 = unpack(py_tuple({ self.attr("a"), self.attr("b"), self.attr("b2j"), self.attr("bjunk").attr("__contains__") }), 4);
                        a_ = items14[0];
                        b = items14[1];
                        b2j = items14[2];
                        isbjunk = items14[3];
                        if (ahi.is_none())
                        {
                            ahi = py{ a_.size() };
                        }
                        if (bhi.is_none())
                        {
                            bhi = py{ b.size() };
                        }
                        auto const items15 = unpack(py_tuple({ alo, blo, I<0>() }), 3);
                        besti = items15[0];
                        bestj = items15[1];
                        bestsize = items15[2];
                        // find longest junk-free match
                        // during an iteration of the loop, j2len[j] = length of longest
                        // junk-free match ending with a[i-1] and b[j]
                        j2len = py_dict();
                        nothing = py_list();
                        for (py const& item : iterate{ (B<"range">())(alo, ahi) })
                        {
                            i = item;
                            // look at all instances of a[i] in b; note that because
                            // b2j has no junk keys, the loop is skipped if a[i] is junk
                            j2lenget = j2len.attr("get");
                            newj2len = py_dict();
                            for (py const& item : iterate{ b2j.method("get", { a_[i], nothing }) })
                            {
                                j = item;
                                // a[i] matches b[j]
                                if (j < blo)
                                {
                                    continue;
                                }
                                if (j >= bhi)
                                {
                                    break;
                                }
                                py const value18 = j2lenget(j - (I<1>()), I<0>()) + (I<1>());
                                k = value18;
                                newj2len.set_item(j, value18);
                                if (k > bestsize)
                                {
                                    auto const items19 = unpack(py_tuple({ (i - k) + (I<1>()), (j - k) + (I<1>()), k }), 3);
                                    besti = items19[0];
                                    bestj = items19[1];
                                    bestsize = items19[2];
                                }
                            }
                            j2len = newj2len;
                        }
                        // Extend the best by non-junk elements on each end.  In particular,
                        // "popular" non-junk elements aren't in b2j, which greatly speeds
                        // the inner loop above, but also means "the best" match so far
                        // doesn't contain any junk *or* popular non-junk elements.
                        while (((besti > alo) && (bestj > blo) && (!isbjunk(b[bestj - (I<1>())]).truthy()) && (a_[besti - (I<1>())] == b[bestj - (I<1>())])))
                        {
                            auto const items21 = unpack(py_tuple({ besti - (I<1>()), bestj - (I<1>()), bestsize + (I<1>()) }), 3);
                            besti = items21[0];
                            bestj = items21[1];
                            bestsize = items21[2];
                        }
                        while ((((besti + bestsize) < ahi) && ((bestj + bestsize) < bhi) && (!isbjunk(b[bestj + bestsize]).truthy()) && (a_[besti + bestsize] == b[bestj + bestsize])))
                        {
                            bestsize = inplace(NEOS_PY_ADD, bestsize, I<1>());
                        }
                        // Now that we have a wholly interesting match (albeit possibly
                        // empty!), we may as well suck up the matching junk on each
                        // side of it too.  Can't think of a good reason not to, and it
                        // saves post-processing the (possibly considerable) expense of
                        // figuring out what to do with it.  In the case of an empty
                        // interesting match, this is clearly the right thing to do,
                        // because no other kind of match is possible in the regions.
                        while (((besti > alo) && (bestj > blo) && isbjunk(b[bestj - (I<1>())]).truthy() && (a_[besti - (I<1>())] == b[bestj - (I<1>())])))
                        {
                            auto const items24 = unpack(py_tuple({ besti - (I<1>()), bestj - (I<1>()), bestsize + (I<1>()) }), 3);
                            besti = items24[0];
                            bestj = items24[1];
                            bestsize = items24[2];
                        }
                        while ((((besti + bestsize) < ahi) && ((bestj + bestsize) < bhi) && isbjunk(b[bestj + bestsize]).truthy() && (a_[besti + bestsize] == b[bestj + bestsize])))
                        {
                            bestsize = bestsize + (I<1>());
                        }
                        return (G<"difflib.Match">())(besti, bestj, bestsize);
                    }) },
                { "get_matching_blocks", w.method("SequenceMatcher", "get_matching_blocks", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py ahi;
                        py alo;
                        py bhi;
                        py blo;
                        py i;
                        py i1;
                        py i2;
                        py j;
                        py j1;
                        py j2;
                        py k;
                        py k1;
                        py k2;
                        py la;
                        py lb;
                        py matching_blocks;
                        py non_adjacent;
                        py queue;
                        py x;
                        if (!self.attr("matching_blocks").is_none())
                        {
                            return self.attr("matching_blocks");
                        }
                        auto const items26 = unpack(py_tuple({ py{ self.attr("a").size() }, py{ self.attr("b").size() } }), 2);
                        la = items26[0];
                        lb = items26[1];
                        // This is most naturally expressed as a recursive algorithm, but
                        // at least one user bumped into extreme use cases that exceeded
                        // the recursion limit on their box.  So, now we maintain a list
                        // ('queue`) of blocks we still need to look at, and append partial
                        // results to `matching_blocks` in a loop; the matches are sorted
                        // at the end.
                        queue = py_list({ py_tuple({ I<0>(), la, I<0>(), lb }) });
                        matching_blocks = py_list();
                        while (queue.truthy())
                        {
                            auto const items28 = unpack(queue.method("pop"), 4);
                            alo = items28[0];
                            ahi = items28[1];
                            blo = items28[2];
                            bhi = items28[3];
                            py const value29 = self.method("find_longest_match", { alo, ahi, blo, bhi });
                            auto const items30 = unpack(value29, 3);
                            i = items30[0];
                            j = items30[1];
                            k = items30[2];
                            x = value29;
                            // a[alo:i] vs b[blo:j] unknown
                            // a[i:i+k] same as b[j:j+k]
                            // a[i+k:ahi] vs b[j+k:bhi] unknown
                            if (k.truthy())
                            {
                                matching_blocks.method("append", { x });
                                if (((alo < i) && (blo < j)))
                                {
                                    queue.method("append", { py_tuple({ alo, i, blo, j }) });
                                }
                                if ((((i + k) < ahi) && ((j + k) < bhi)))
                                {
                                    queue.method("append", { py_tuple({ i + k, ahi, j + k, bhi }) });
                                }
                            }
                        }
                        matching_blocks.method("sort");
                        // It's possible that we have adjacent equal blocks in the
                        // matching_blocks list now.  Starting with 2.5, this code was added
                        // to collapse them.
                        py const value31 = I<0>();
                        i1 = value31;
                        j1 = value31;
                        k1 = value31;
                        non_adjacent = py_list();
                        for (py const& item : iterate{ matching_blocks })
                        {
                            auto const items33 = unpack(item, 3);
                            i2 = items33[0];
                            j2 = items33[1];
                            k2 = items33[2];
                            // Is this block adjacent to i1, j1, k1?
                            if ((((i1 + k1) == i2) && ((j1 + k1) == j2)))
                            {
                                // Yes, so collapse them -- this just increases the length of
                                // the first block by the length of the second, and the first
                                // block so lengthened remains the block to compare against.
                                k1 = inplace(NEOS_PY_ADD, k1, k2);
                            }
                            else
                            {
                                // Not adjacent.  Remember the first block (k1==0 means it's
                                // the dummy we started with), and make the second block the
                                // new block to compare against.
                                if (k1.truthy())
                                {
                                    non_adjacent.method("append", { py_tuple({ i1, j1, k1 }) });
                                }
                                auto const items34 = unpack(py_tuple({ i2, j2, k2 }), 3);
                                i1 = items34[0];
                                j1 = items34[1];
                                k1 = items34[2];
                            }
                        }
                        if (k1.truthy())
                        {
                            non_adjacent.method("append", { py_tuple({ i1, j1, k1 }) });
                        }
                        non_adjacent.method("append", { py_tuple({ la, lb, I<0>() }) });
                        self.set_attr("matching_blocks", (B<"list">())((B<"map">())((G<"difflib.Match">()).attr("_make"), non_adjacent)));
                        return self.attr("matching_blocks");
                    }) },
                { "get_opcodes", w.method("SequenceMatcher", "get_opcodes", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py ai;
                        py answer;
                        py bj;
                        py i;
                        py j;
                        py size;
                        py tag;
                        if (!self.attr("opcodes").is_none())
                        {
                            return self.attr("opcodes");
                        }
                        py const value35 = I<0>();
                        i = value35;
                        j = value35;
                        py const value36 = py_list();
                        self.set_attr("opcodes", value36);
                        answer = value36;
                        for (py const& item : iterate{ self.method("get_matching_blocks") })
                        {
                            auto const items38 = unpack(item, 3);
                            ai = items38[0];
                            bj = items38[1];
                            size = items38[2];
                            // invariant:  we've pumped out correct diffs to change
                            // a[:i] into b[:j], and the next matching block is
                            // a[ai:ai+size] == b[bj:bj+size].  So we need to pump
                            // out a diff to change a[i:ai] into b[j:bj], pump out
                            // the matching block, and move (i,j) beyond the match
                            tag = S<"">();
                            if (((i < ai) && (j < bj)))
                            {
                                tag = S<"replace">();
                            }
                            else if (i < ai)
                            {
                                tag = S<"delete">();
                            }
                            else if (j < bj)
                            {
                                tag = S<"insert">();
                            }
                            if (tag.truthy())
                            {
                                answer.method("append", { py_tuple({ tag, i, ai, j, bj }) });
                            }
                            auto const items39 = unpack(py_tuple({ ai + size, bj + size }), 2);
                            i = items39[0];
                            j = items39[1];
                            // the list of matching blocks is terminated by a
                            // sentinel with size 0
                            if (size.truthy())
                            {
                                answer.method("append", { py_tuple({ S<"equal">(), ai, i, bj, j }) });
                            }
                        }
                        return answer;
                    }) },
                { "get_grouped_opcodes", w.method("SequenceMatcher", "get_grouped_opcodes", { py_positional("n", I<3>()) }, [](arguments a) -> py
                    {
                        return make_native_generator("get_grouped_opcodes", "SequenceMatcher.get_grouped_opcodes", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                        {
                            py self = a[0];
                            py n = a[1];
                            py codes;
                            py group;
                            py i1;
                            py i2;
                            py j1;
                            py j2;
                            py nn;
                            py tag;
                            codes = self.method("get_opcodes");
                            if (!codes.truthy())
                            {
                                codes = py_list({ py_tuple({ S<"equal">(), I<0>(), I<1>(), I<0>(), I<1>() }) });
                            }
                            // Fixup leading and trailing groups if they show no changes.
                            if (codes[I<0>()][I<0>()] == (S<"equal">()))
                            {
                                auto const items40 = unpack(codes[I<0>()], 5);
                                tag = items40[0];
                                i1 = items40[1];
                                i2 = items40[2];
                                j1 = items40[3];
                                j2 = items40[4];
                                codes.set_item(I<0>(), py_tuple({ tag, (B<"max">())(i1, i2 - n), i2, (B<"max">())(j1, j2 - n), j2 }));
                            }
                            if (codes[I<-1>()][I<0>()] == (S<"equal">()))
                            {
                                auto const items41 = unpack(codes[I<-1>()], 5);
                                tag = items41[0];
                                i1 = items41[1];
                                i2 = items41[2];
                                j1 = items41[3];
                                j2 = items41[4];
                                codes.set_item(I<-1>(), py_tuple({ tag, i1, (B<"min">())(i2, i1 + n), j1, (B<"min">())(j2, j1 + n) }));
                            }
                            nn = n + n;
                            group = py_list();
                            for (py const& item : iterate{ codes })
                            {
                                auto const items43 = unpack(item, 5);
                                tag = items43[0];
                                i1 = items43[1];
                                i2 = items43[2];
                                j1 = items43[3];
                                j2 = items43[4];
                                // End the current group and start a new one whenever
                                // there is a large range with no changes.
                                if (((tag == (S<"equal">())) && ((i2 - i1) > nn)))
                                {
                                    group.method("append", { py_tuple({ tag, i1, (B<"min">())(i2, i1 + n), j1, (B<"min">())(j2, j1 + n) }) });
                                    py_yield(group);
                                    group = py_list();
                                    auto const items44 = unpack(py_tuple({ (B<"max">())(i1, i2 - n), (B<"max">())(j1, j2 - n) }), 2);
                                    i1 = items44[0];
                                    j1 = items44[1];
                                }
                                group.method("append", { py_tuple({ tag, i1, i2, j1, j2 }) });
                            }
                            if ((group.truthy() && (!([&]() -> py { py const t = boolean(py{ group.size() } == (I<1>())); return !t.truthy() ? t : boolean(group[I<0>()][I<0>()] == (S<"equal">())); }()).truthy())))
                            {
                                py_yield(group);
                            }
                            return none();
                        });
                    }) },
                { "ratio", w.method("SequenceMatcher", "ratio", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py matches_;
                        matches_ = (B<"sum">())([&]() -> py { py const comprehension45 = py_list(); py triple; for (py const& item : iterate{ self.method("get_matching_blocks") }) { triple = item; comprehension45.method("append", { triple[I<-1>()] }); } return comprehension45; }());
                        return (G<"difflib._calculate_ratio">())(matches_, py{ self.attr("a").size() } + py{ self.attr("b").size() });
                    }) },
                { "quick_ratio", w.method("SequenceMatcher", "quick_ratio", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py avail;
                        py availhas;
                        py elt;
                        py fullbcount;
                        py matches_;
                        py numb;
                        // viewing a and b as multisets, set matches to the cardinality
                        // of their intersection; this counts the number of matches
                        // without regard to order, so is clearly an upper bound
                        if (self.attr("fullbcount").is_none())
                        {
                            py const value46 = py_dict();
                            self.set_attr("fullbcount", value46);
                            fullbcount = value46;
                            for (py const& item : iterate{ self.attr("b") })
                            {
                                elt = item;
                                fullbcount.set_item(elt, fullbcount.method("get", { elt, I<0>() }) + (I<1>()));
                            }
                        }
                        fullbcount = self.attr("fullbcount");
                        // avail[x] is the number of times x appears in 'b' less the
                        // number of times we've seen it in 'a' so far ... kinda
                        avail = py_dict();
                        auto const items48 = unpack(py_tuple({ avail.attr("__contains__"), I<0>() }), 2);
                        availhas = items48[0];
                        matches_ = items48[1];
                        for (py const& item : iterate{ self.attr("a") })
                        {
                            elt = item;
                            if (availhas(elt).truthy())
                            {
                                numb = avail[elt];
                            }
                            else
                            {
                                numb = fullbcount.method("get", { elt, I<0>() });
                            }
                            avail.set_item(elt, numb - (I<1>()));
                            if (numb > (I<0>()))
                            {
                                matches_ = matches_ + (I<1>());
                            }
                        }
                        return (G<"difflib._calculate_ratio">())(matches_, py{ self.attr("a").size() } + py{ self.attr("b").size() });
                    }) },
                { "real_quick_ratio", w.method("SequenceMatcher", "real_quick_ratio", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py la;
                        py lb;
                        auto const items50 = unpack(py_tuple({ py{ self.attr("a").size() }, py{ self.attr("b").size() } }), 2);
                        la = items50[0];
                        lb = items50[1];
                        // can't have more matches than the number of elements in the
                        // shorter sequence
                        return (G<"difflib._calculate_ratio">())((B<"min">())(la, lb), la + lb);
                    }) },
                { "__class_getitem__", (B<"classmethod">())(Mod<"types">().attr("GenericAlias")) }, }));

            define("difflib", "get_close_matches", w.function("get_close_matches", "get_close_matches", { py_positional("word"), py_positional("possibilities"), py_positional("n", I<3>()), py_positional("cutoff", F<0.6>()) }, [](arguments a) -> py
                {
                    py word = a[0];
                    py possibilities = a[1];
                    py n = a[2];
                    py cutoff = a[3];
                    py result;
                    py s;
                    py x;
                    if (!(n > (I<0>())))
                    {
                        py_raise((B<"ValueError">())((S<"n must be > 0: %r">()) % py_tuple({ n })));
                    }
                    if (!(((F<0.0>()) <= cutoff) && (cutoff <= (F<1.0>()))))
                    {
                        py_raise((B<"ValueError">())((S<"cutoff must be in [0.0, 1.0]: %r">()) % py_tuple({ cutoff })));
                    }
                    result = py_list();
                    s = (G<"difflib.SequenceMatcher">())();
                    s.method("set_seq2", { word });
                    for (py const& item : iterate{ possibilities })
                    {
                        x = item;
                        s.method("set_seq1", { x });
                        if (((s.method("real_quick_ratio") >= cutoff) && (s.method("quick_ratio") >= cutoff) && (s.method("ratio") >= cutoff)))
                        {
                            result.method("append", { py_tuple({ s.method("ratio"), x }) });
                        }
                    }
                    // Move the best scorers to head of list
                    result = (Mod<"heapq">().attr("nlargest"))(n, result);
                    // Strip scores for the best n matches
                    return [&]() -> py { py const comprehension52 = py_list(); py score; py x; for (py const& item : iterate{ result }) { auto const items53 = unpack(item, 2); score = items53[0]; x = items53[1]; comprehension52.method("append", { x }); } return comprehension52; }();
                }));

            define("difflib", "_keep_original_ws", w.function("_keep_original_ws", "_keep_original_ws", { py_positional("s"), py_positional("tag_s") }, [](arguments a) -> py
                {
                    py s = a[0];
                    py tag_s = a[1];
                    return (S<"">()).method("join", { [&]() -> py { py const comprehension54 = py_list(); py c; py tag_c; for (py const& item : iterate{ (B<"zip">())(s, tag_s) }) { auto const items55 = unpack(item, 2); c = items55[0]; tag_c = items55[1]; comprehension54.method("append", { (([&]() -> py { py const t = boolean(tag_c == (S<" ">())); return !t.truthy() ? t : c.method("isspace"); }()).truthy() ? c : tag_c) }); } return comprehension54; }() });
                }));

            define("difflib", "Differ", w.make_class("Differ", {  }, {
                { "__init__", w.method("Differ", "__init__", { py_positional("linejunk", none()), py_positional("charjunk", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py linejunk = a[1];
                        py charjunk = a[2];
                        self.set_attr("linejunk", linejunk);
                        self.set_attr("charjunk", charjunk);
                        return none();
                    }) },
                { "compare", w.method("Differ", "compare", { py_positional("a"), py_positional("b") }, [](arguments a) -> py
                    {
                        return make_native_generator("compare", "Differ.compare", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                        {
                            py self = a[0];
                            py a_ = a[1];
                            py b = a[2];
                            py ahi;
                            py alo;
                            py bhi;
                            py blo;
                            py cruncher;
                            py g;
                            py tag;
                            cruncher = (G<"difflib.SequenceMatcher">())(self.attr("linejunk"), a_, b);
                            for (py const& item : iterate{ cruncher.method("get_opcodes") })
                            {
                                auto const items57 = unpack(item, 5);
                                tag = items57[0];
                                alo = items57[1];
                                ahi = items57[2];
                                blo = items57[3];
                                bhi = items57[4];
                                if (tag == (S<"replace">()))
                                {
                                    g = self.method("_fancy_replace", { a_, alo, ahi, b, blo, bhi });
                                }
                                else if (tag == (S<"delete">()))
                                {
                                    g = self.method("_dump", { S<"-">(), a_, alo, ahi });
                                }
                                else if (tag == (S<"insert">()))
                                {
                                    g = self.method("_dump", { S<"+">(), b, blo, bhi });
                                }
                                else if (tag == (S<"equal">()))
                                {
                                    g = self.method("_dump", { S<" ">(), a_, alo, ahi });
                                }
                                else
                                {
                                    py_raise((B<"ValueError">())((S<"unknown tag %r">()) % py_tuple({ tag })));
                                }
                                yield_from(g);
                            }
                            return none();
                        });
                    }) },
                { "_dump", w.method("Differ", "_dump", { py_positional("tag"), py_positional("x"), py_positional("lo"), py_positional("hi") }, [](arguments a) -> py
                    {
                        return make_native_generator("_dump", "Differ._dump", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                        {
                            py self = a[0];
                            py tag = a[1];
                            py x = a[2];
                            py lo = a[3];
                            py hi = a[4];
                            py i;
                            for (py const& item : iterate{ (B<"range">())(lo, hi) })
                            {
                                i = item;
                                py_yield((S<"%s %s">()) % py_tuple({ tag, x[i] }));
                            }
                            return none();
                        });
                    }) },
                { "_plain_replace", w.method("Differ", "_plain_replace", { py_positional("a"), py_positional("alo"), py_positional("ahi"), py_positional("b"), py_positional("blo"), py_positional("bhi") }, [](arguments a) -> py
                    {
                        return make_native_generator("_plain_replace", "Differ._plain_replace", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                        {
                            py self = a[0];
                            py a_ = a[1];
                            py alo = a[2];
                            py ahi = a[3];
                            py b = a[4];
                            py blo = a[5];
                            py bhi = a[6];
                            py first;
                            py g;
                            py second;
                            if (!([&]() -> py { py const t = boolean(alo < ahi); return !t.truthy() ? t : boolean(blo < bhi); }()).truthy())
                                py_raise(B<"AssertionError">()());
                            // dump the shorter block first -- reduces the burden on short-term
                            // memory if the blocks are of very different sizes
                            if ((bhi - blo) < (ahi - alo))
                            {
                                first = self.method("_dump", { S<"+">(), b, blo, bhi });
                                second = self.method("_dump", { S<"-">(), a_, alo, ahi });
                            }
                            else
                            {
                                first = self.method("_dump", { S<"-">(), a_, alo, ahi });
                                second = self.method("_dump", { S<"+">(), b, blo, bhi });
                            }
                            for (py const& item : iterate{ py_tuple({ first, second }) })
                            {
                                g = item;
                                yield_from(g);
                            }
                            return none();
                        });
                    }) },
                { "_fancy_replace", w.method("Differ", "_fancy_replace", { py_positional("a"), py_positional("alo"), py_positional("ahi"), py_positional("b"), py_positional("blo"), py_positional("bhi") }, [](arguments a) -> py
                    {
                        return make_native_generator("_fancy_replace", "Differ._fancy_replace", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                        {
                            py self = a[0];
                            py a_ = a[1];
                            py alo = a[2];
                            py ahi = a[3];
                            py b = a[4];
                            py blo = a[5];
                            py bhi = a[6];
                            py aelt;
                            py ai;
                            py ai1;
                            py ai2;
                            py atags;
                            py belt;
                            py best_i;
                            py best_j;
                            py best_ratio;
                            py bj;
                            py bj1;
                            py bj2;
                            py btags;
                            py cruncher;
                            py cutoff;
                            py eqi;
                            py eqj;
                            py i;
                            py j;
                            py la;
                            py lb;
                            py tag;
                            // don't synch up unless the lines have a similarity score of at
                            // least cutoff; best_ratio tracks the best score seen so far
                            auto const items60 = unpack(py_tuple({ F<0.74>(), F<0.75>() }), 2);
                            best_ratio = items60[0];
                            cutoff = items60[1];
                            cruncher = (G<"difflib.SequenceMatcher">())(self.attr("charjunk"));
                            auto const items61 = unpack(py_tuple({ none(), none() }), 2);
                            eqi = items61[0];
                            eqj = items61[1];
                            // search for the pair that matches best without being identical
                            // (identical lines must be junk lines, & we don't want to synch up
                            // on junk -- unless we have to)
                            for (py const& item : iterate{ (B<"range">())(blo, bhi) })
                            {
                                j = item;
                                bj = b[j];
                                cruncher.method("set_seq2", { bj });
                                for (py const& item : iterate{ (B<"range">())(alo, ahi) })
                                {
                                    i = item;
                                    ai = a_[i];
                                    if (ai == bj)
                                    {
                                        if (eqi.is_none())
                                        {
                                            auto const items64 = unpack(py_tuple({ i, j }), 2);
                                            eqi = items64[0];
                                            eqj = items64[1];
                                        }
                                        continue;
                                    }
                                    cruncher.method("set_seq1", { ai });
                                    // computing similarity is expensive, so use the quick
                                    // upper bounds first -- have seen this speed up messy
                                    // compares by a factor of 3.
                                    // note that ratio() is only expensive to compute the first
                                    // time it's called on a sequence pair; the expensive part
                                    // of the computation is cached by cruncher
                                    if (((cruncher.method("real_quick_ratio") > best_ratio) && (cruncher.method("quick_ratio") > best_ratio) && (cruncher.method("ratio") > best_ratio)))
                                    {
                                        auto const items65 = unpack(py_tuple({ cruncher.method("ratio"), i, j }), 3);
                                        best_ratio = items65[0];
                                        best_i = items65[1];
                                        best_j = items65[2];
                                    }
                                }
                            }
                            if (best_ratio < cutoff)
                            {
                                // no non-identical "pretty close" pair
                                if (eqi.is_none())
                                {
                                    // no identical pair either -- treat it as a straight replace
                                    yield_from(self.method("_plain_replace", { a_, alo, ahi, b, blo, bhi }));
                                    return none();
                                }
                                // no close pair, but an identical pair -- synch up on that
                                auto const items66 = unpack(py_tuple({ eqi, eqj, F<1.0>() }), 3);
                                best_i = items66[0];
                                best_j = items66[1];
                                best_ratio = items66[2];
                            }
                            else
                            {
                                // there's a close pair, so forget the identical pair (if any)
                                eqi = none();
                            }
                            // a[best_i] very similar to b[best_j]; eqi is None iff they're not
                            // identical
                            // pump out diffs from before the synch point
                            yield_from(self.method("_fancy_helper", { a_, alo, best_i, b, blo, best_j }));
                            // do intraline marking on the synch pair
                            auto const items67 = unpack(py_tuple({ a_[best_i], b[best_j] }), 2);
                            aelt = items67[0];
                            belt = items67[1];
                            if (eqi.is_none())
                            {
                                // pump out a '-', '?', '+', '?' quad for the synched lines
                                py const value68 = S<"">();
                                atags = value68;
                                btags = value68;
                                cruncher.method("set_seqs", { aelt, belt });
                                for (py const& item : iterate{ cruncher.method("get_opcodes") })
                                {
                                    auto const items70 = unpack(item, 5);
                                    tag = items70[0];
                                    ai1 = items70[1];
                                    ai2 = items70[2];
                                    bj1 = items70[3];
                                    bj2 = items70[4];
                                    auto const items71 = unpack(py_tuple({ ai2 - ai1, bj2 - bj1 }), 2);
                                    la = items71[0];
                                    lb = items71[1];
                                    if (tag == (S<"replace">()))
                                    {
                                        atags = inplace(NEOS_PY_ADD, atags, (S<"^">()) * la);
                                        btags = inplace(NEOS_PY_ADD, btags, (S<"^">()) * lb);
                                    }
                                    else if (tag == (S<"delete">()))
                                    {
                                        atags = inplace(NEOS_PY_ADD, atags, (S<"-">()) * la);
                                    }
                                    else if (tag == (S<"insert">()))
                                    {
                                        btags = inplace(NEOS_PY_ADD, btags, (S<"+">()) * lb);
                                    }
                                    else if (tag == (S<"equal">()))
                                    {
                                        atags = inplace(NEOS_PY_ADD, atags, (S<" ">()) * la);
                                        btags = inplace(NEOS_PY_ADD, btags, (S<" ">()) * lb);
                                    }
                                    else
                                    {
                                        py_raise((B<"ValueError">())((S<"unknown tag %r">()) % py_tuple({ tag })));
                                    }
                                }
                                yield_from(self.method("_qformat", { aelt, belt, atags, btags }));
                            }
                            else
                            {
                                // the synch pair is identical
                                py_yield((S<"  ">()) + aelt);
                            }
                            // pump out diffs from after the synch point
                            yield_from(self.method("_fancy_helper", { a_, best_i + (I<1>()), ahi, b, best_j + (I<1>()), bhi }));
                            return none();
                        });
                    }) },
                { "_fancy_helper", w.method("Differ", "_fancy_helper", { py_positional("a"), py_positional("alo"), py_positional("ahi"), py_positional("b"), py_positional("blo"), py_positional("bhi") }, [](arguments a) -> py
                    {
                        return make_native_generator("_fancy_helper", "Differ._fancy_helper", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                        {
                            py self = a[0];
                            py a_ = a[1];
                            py alo = a[2];
                            py ahi = a[3];
                            py b = a[4];
                            py blo = a[5];
                            py bhi = a[6];
                            py g;
                            g = py_list();
                            if (alo < ahi)
                            {
                                if (blo < bhi)
                                {
                                    g = self.method("_fancy_replace", { a_, alo, ahi, b, blo, bhi });
                                }
                                else
                                {
                                    g = self.method("_dump", { S<"-">(), a_, alo, ahi });
                                }
                            }
                            else if (blo < bhi)
                            {
                                g = self.method("_dump", { S<"+">(), b, blo, bhi });
                            }
                            yield_from(g);
                            return none();
                        });
                    }) },
                { "_qformat", w.method("Differ", "_qformat", { py_positional("aline"), py_positional("bline"), py_positional("atags"), py_positional("btags") }, [](arguments a) -> py
                    {
                        return make_native_generator("_qformat", "Differ._qformat", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                        {
                            py self = a[0];
                            py aline = a[1];
                            py bline = a[2];
                            py atags = a[3];
                            py btags = a[4];
                            atags = (G<"difflib._keep_original_ws">())(aline, atags).method("rstrip");
                            btags = (G<"difflib._keep_original_ws">())(bline, btags).method("rstrip");
                            py_yield((S<"- ">()) + aline);
                            if (atags.truthy())
                            {
                                py_yield(S(std::string{ "? " } + atags.str() + "\n"));
                            }
                            py_yield((S<"+ ">()) + bline);
                            if (btags.truthy())
                            {
                                py_yield(S(std::string{ "? " } + btags.str() + "\n"));
                            }
                            return none();
                        });
                    }) }, }));

            // With respect to junk, an earlier version of ndiff simply refused to
            // *start* a match with a junk element.  The result was cases like this:
            // before: private Thread currentThread;
            // after:  private volatile Thread currentThread;
            // If you consider whitespace to be junk, the longest contiguous match
            // not starting with junk is "e Thread currentThread".  So ndiff reported
            // that "e volatil" was inserted between the 't' and the 'e' in "private".
            // While an accurate view, to people that's absurd.  The current version
            // looks for matching blocks that are entirely junk-free, then extends the
            // longest one of those as far as possible but only with matching junk.
            // So now "currentThread" is matched, then extended to suck up the
            // preceding blank; then "private" is matched, and extended to suck up the
            // following blank; then "Thread" is matched; and finally ndiff reports
            // that "volatile " was inserted before "Thread".  The only quibble
            // remaining is that perhaps it was really the case that " volatile"
            // was inserted after "private".  I can live with that <wink>.
            define("difflib", "IS_LINE_JUNK", w.function("IS_LINE_JUNK", "IS_LINE_JUNK", { py_positional("line"), py_positional("pat", (Mod<"re">()).method("compile", { S<"\\s*(?:#\\s*)?$">() }).attr("match")) }, [](arguments a) -> py
                {
                    py line = a[0];
                    py pat = a[1];
                    return boolean(!pat(line).is_none());
                }));

            define("difflib", "IS_CHARACTER_JUNK", w.function("IS_CHARACTER_JUNK", "IS_CHARACTER_JUNK", { py_positional("ch"), py_positional("ws", S<" \t">()) }, [](arguments a) -> py
                {
                    py ch = a[0];
                    py ws = a[1];
                    return boolean(ws.contains(ch));
                }));

            // #######################################################################
            // ##  Unified Diff
            // #######################################################################
            define("difflib", "_format_range_unified", w.function("_format_range_unified", "_format_range_unified", { py_positional("start"), py_positional("stop") }, [](arguments a) -> py
                {
                    py start = a[0];
                    py stop = a[1];
                    py beginning;
                    py length;
                    // Per the diff spec at http://www.unix.org/single_unix_specification/
                    beginning = start + (I<1>());
                    length = stop - start;
                    if (length == (I<1>()))
                    {
                        return (S<"{}">()).method("format", { beginning });
                    }
                    if (!length.truthy())
                    {
                        beginning = inplace(NEOS_PY_SUBTRACT, beginning, I<1>());
                    }
                    return (S<"{},{}">()).method("format", { beginning, length });
                }));

            define("difflib", "unified_diff", w.function("unified_diff", "unified_diff", { py_positional("a"), py_positional("b"), py_positional("fromfile", S<"">()), py_positional("tofile", S<"">()), py_positional("fromfiledate", S<"">()), py_positional("tofiledate", S<"">()), py_positional("n", I<3>()), py_positional("lineterm", S<"\n">()) }, [](arguments a) -> py
                {
                    return make_native_generator("unified_diff", "unified_diff", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                    {
                        py a_ = a[0];
                        py b = a[1];
                        py fromfile = a[2];
                        py tofile = a[3];
                        py fromfiledate = a[4];
                        py tofiledate = a[5];
                        py n = a[6];
                        py lineterm = a[7];
                        py file1_range;
                        py file2_range;
                        py first;
                        py fromdate;
                        py group;
                        py i1;
                        py i2;
                        py j1;
                        py j2;
                        py last;
                        py line;
                        py started;
                        py tag;
                        py todate;
                        (G<"difflib._check_types">())(a_, b, fromfile, tofile, fromfiledate, tofiledate, lineterm);
                        started = boolean(false);
                        for (py const& item : iterate{ (G<"difflib.SequenceMatcher">())(none(), a_, b).method("get_grouped_opcodes", { n }) })
                        {
                            group = item;
                            if (!started.truthy())
                            {
                                started = boolean(true);
                                fromdate = (fromfiledate.truthy() ? (S<"\t{}">()).method("format", { fromfiledate }) : S<"">());
                                todate = (tofiledate.truthy() ? (S<"\t{}">()).method("format", { tofiledate }) : S<"">());
                                py_yield((S<"--- {}{}{}">()).method("format", { fromfile, fromdate, lineterm }));
                                py_yield((S<"+++ {}{}{}">()).method("format", { tofile, todate, lineterm }));
                            }
                            auto const items73 = unpack(py_tuple({ group[I<0>()], group[I<-1>()] }), 2);
                            first = items73[0];
                            last = items73[1];
                            file1_range = (G<"difflib._format_range_unified">())(first[I<1>()], last[I<2>()]);
                            file2_range = (G<"difflib._format_range_unified">())(first[I<3>()], last[I<4>()]);
                            py_yield((S<"@@ -{} +{} @@{}">()).method("format", { file1_range, file2_range, lineterm }));
                            for (py const& item : iterate{ group })
                            {
                                auto const items75 = unpack(item, 5);
                                tag = items75[0];
                                i1 = items75[1];
                                i2 = items75[2];
                                j1 = items75[3];
                                j2 = items75[4];
                                if (tag == (S<"equal">()))
                                {
                                    for (py const& item : iterate{ a_[B<"slice">()(i1, i2, none())] })
                                    {
                                        line = item;
                                        py_yield((S<" ">()) + line);
                                    }
                                    continue;
                                }
                                if (set_of({ S<"replace">(), S<"delete">() }).contains(tag))
                                {
                                    for (py const& item : iterate{ a_[B<"slice">()(i1, i2, none())] })
                                    {
                                        line = item;
                                        py_yield((S<"-">()) + line);
                                    }
                                }
                                if (set_of({ S<"replace">(), S<"insert">() }).contains(tag))
                                {
                                    for (py const& item : iterate{ b[B<"slice">()(j1, j2, none())] })
                                    {
                                        line = item;
                                        py_yield((S<"+">()) + line);
                                    }
                                }
                            }
                        }
                        return none();
                    });
                }));

            // #######################################################################
            // ##  Context Diff
            // #######################################################################
            define("difflib", "_format_range_context", w.function("_format_range_context", "_format_range_context", { py_positional("start"), py_positional("stop") }, [](arguments a) -> py
                {
                    py start = a[0];
                    py stop = a[1];
                    py beginning;
                    py length;
                    // Per the diff spec at http://www.unix.org/single_unix_specification/
                    beginning = start + (I<1>());
                    length = stop - start;
                    if (!length.truthy())
                    {
                        beginning = inplace(NEOS_PY_SUBTRACT, beginning, I<1>());
                    }
                    if (length <= (I<1>()))
                    {
                        return (S<"{}">()).method("format", { beginning });
                    }
                    return (S<"{},{}">()).method("format", { beginning, (beginning + length) - (I<1>()) });
                }));

            // See http://www.unix.org/single_unix_specification/
            define("difflib", "context_diff", w.function("context_diff", "context_diff", { py_positional("a"), py_positional("b"), py_positional("fromfile", S<"">()), py_positional("tofile", S<"">()), py_positional("fromfiledate", S<"">()), py_positional("tofiledate", S<"">()), py_positional("n", I<3>()), py_positional("lineterm", S<"\n">()) }, [](arguments a) -> py
                {
                    return make_native_generator("context_diff", "context_diff", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                    {
                        py a_ = a[0];
                        py b = a[1];
                        py fromfile = a[2];
                        py tofile = a[3];
                        py fromfiledate = a[4];
                        py tofiledate = a[5];
                        py n = a[6];
                        py lineterm = a[7];
                        py _;
                        py file1_range;
                        py file2_range;
                        py first;
                        py fromdate;
                        py group;
                        py i1;
                        py i2;
                        py j1;
                        py j2;
                        py last;
                        py line;
                        py prefix;
                        py started;
                        py tag;
                        py todate;
                        (G<"difflib._check_types">())(a_, b, fromfile, tofile, fromfiledate, tofiledate, lineterm);
                        prefix = (B<"dict">()).call({  }, { { "insert", S<"+ ">() }, { "delete", S<"- ">() }, { "replace", S<"! ">() }, { "equal", S<"  ">() } });
                        started = boolean(false);
                        for (py const& item : iterate{ (G<"difflib.SequenceMatcher">())(none(), a_, b).method("get_grouped_opcodes", { n }) })
                        {
                            group = item;
                            if (!started.truthy())
                            {
                                started = boolean(true);
                                fromdate = (fromfiledate.truthy() ? (S<"\t{}">()).method("format", { fromfiledate }) : S<"">());
                                todate = (tofiledate.truthy() ? (S<"\t{}">()).method("format", { tofiledate }) : S<"">());
                                py_yield((S<"*** {}{}{}">()).method("format", { fromfile, fromdate, lineterm }));
                                py_yield((S<"--- {}{}{}">()).method("format", { tofile, todate, lineterm }));
                            }
                            auto const items80 = unpack(py_tuple({ group[I<0>()], group[I<-1>()] }), 2);
                            first = items80[0];
                            last = items80[1];
                            py_yield((S<"***************">()) + lineterm);
                            file1_range = (G<"difflib._format_range_context">())(first[I<1>()], last[I<2>()]);
                            py_yield((S<"*** {} ****{}">()).method("format", { file1_range, lineterm }));
                            if ((B<"any">())([&]() -> py { py const comprehension81 = py_list(); py _; py tag; for (py const& item : iterate{ group }) { auto const items82 = unpack(item, 5); tag = items82[0]; _ = items82[1]; _ = items82[2]; _ = items82[3]; _ = items82[4]; comprehension81.method("append", { boolean(set_of({ S<"replace">(), S<"delete">() }).contains(tag)) }); } return comprehension81; }()).truthy())
                            {
                                for (py const& item : iterate{ group })
                                {
                                    auto const items84 = unpack(item, 5);
                                    tag = items84[0];
                                    i1 = items84[1];
                                    i2 = items84[2];
                                    _ = items84[3];
                                    _ = items84[4];
                                    if (tag != (S<"insert">()))
                                    {
                                        for (py const& item : iterate{ a_[B<"slice">()(i1, i2, none())] })
                                        {
                                            line = item;
                                            py_yield(prefix[tag] + line);
                                        }
                                    }
                                }
                            }
                            file2_range = (G<"difflib._format_range_context">())(first[I<3>()], last[I<4>()]);
                            py_yield((S<"--- {} ----{}">()).method("format", { file2_range, lineterm }));
                            if ((B<"any">())([&]() -> py { py const comprehension86 = py_list(); py _; py tag; for (py const& item : iterate{ group }) { auto const items87 = unpack(item, 5); tag = items87[0]; _ = items87[1]; _ = items87[2]; _ = items87[3]; _ = items87[4]; comprehension86.method("append", { boolean(set_of({ S<"replace">(), S<"insert">() }).contains(tag)) }); } return comprehension86; }()).truthy())
                            {
                                for (py const& item : iterate{ group })
                                {
                                    auto const items89 = unpack(item, 5);
                                    tag = items89[0];
                                    _ = items89[1];
                                    _ = items89[2];
                                    j1 = items89[3];
                                    j2 = items89[4];
                                    if (tag != (S<"delete">()))
                                    {
                                        for (py const& item : iterate{ b[B<"slice">()(j1, j2, none())] })
                                        {
                                            line = item;
                                            py_yield(prefix[tag] + line);
                                        }
                                    }
                                }
                            }
                        }
                        return none();
                    });
                }));

            define("difflib", "_check_types", w.function("_check_types", "_check_types", { py_positional("a"), py_positional("b") }, [](arguments a) -> py
                {
                    py a_ = a[0];
                    py b = a[1];
                    py args = a[2];
                    py arg;
                    if ((a_.truthy() && (!isinstance(a_[I<0>()], B<"str">()))))
                    {
                        py_raise((B<"TypeError">())((S<"lines to compare must be str, not %s (%r)">()) % py_tuple({ (B<"type">())(a_[I<0>()]).attr("__name__"), a_[I<0>()] })));
                    }
                    if ((b.truthy() && (!isinstance(b[I<0>()], B<"str">()))))
                    {
                        py_raise((B<"TypeError">())((S<"lines to compare must be str, not %s (%r)">()) % py_tuple({ (B<"type">())(b[I<0>()]).attr("__name__"), b[I<0>()] })));
                    }
                    for (py const& item : iterate{ args })
                    {
                        arg = item;
                        if (!isinstance(arg, B<"str">()))
                        {
                            py_raise((B<"TypeError">())((S<"all arguments must be str, not: %r">()) % py_tuple({ arg })));
                        }
                    }
                    return none();
                }, true));

            define("difflib", "diff_bytes", w.function("diff_bytes", "diff_bytes", { py_positional("dfunc"), py_positional("a"), py_positional("b"), py_positional("fromfile", py{ make_bytes(std::string{ "", 0 }) }), py_positional("tofile", py{ make_bytes(std::string{ "", 0 }) }), py_positional("fromfiledate", py{ make_bytes(std::string{ "", 0 }) }), py_positional("tofiledate", py{ make_bytes(std::string{ "", 0 }) }), py_positional("n", I<3>()), py_positional("lineterm", py{ make_bytes(std::string{ "\x0a", 1 }) }) }, [](arguments a) -> py
                {
                    return make_native_generator("diff_bytes", "diff_bytes", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                    {
                        py dfunc = a[0];
                        py a_ = a[1];
                        py b = a[2];
                        py fromfile = a[3];
                        py tofile = a[4];
                        py fromfiledate = a[5];
                        py tofiledate = a[6];
                        py n = a[7];
                        py lineterm = a[8];
                        py decode;
                        py line;
                        py lines;
                        decode = w.function("decode", "diff_bytes.<locals>.decode", { py_positional("s") }, [](arguments a) -> py
    {
        py s = a[0];
        py err;
        py msg;
        try
        {
            return s.method("decode", { S<"ascii">(), S<"surrogateescape">() });
        }
        catch (raised const&)
        {
            pyclass::handler_scope handler92;
            if (matches(handler92.exception, B<"AttributeError">()))
            {
                err = handler92.exception;
                msg = (S<"all arguments must be bytes, not %s (%r)">()) % py_tuple({ (B<"type">())(s).attr("__name__"), s });
                fn_raise_from((B<"TypeError">())(msg).get(), err.get());
                throw raised{};
            }
            else
                py_reraise(handler92.exception);
        }
        return none();
    });
                        a_ = (B<"list">())((B<"map">())(decode, a_));
                        b = (B<"list">())((B<"map">())(decode, b));
                        fromfile = decode(fromfile);
                        tofile = decode(tofile);
                        fromfiledate = decode(fromfiledate);
                        tofiledate = decode(tofiledate);
                        lineterm = decode(lineterm);
                        lines = dfunc(a_, b, fromfile, tofile, fromfiledate, tofiledate, n, lineterm);
                        for (py const& item : iterate{ lines })
                        {
                            line = item;
                            py_yield(line.method("encode", { S<"ascii">(), S<"surrogateescape">() }));
                        }
                        return none();
                    });
                }));

            define("difflib", "ndiff", w.function("ndiff", "ndiff", { py_positional("a"), py_positional("b"), py_positional("linejunk", none()), py_positional("charjunk", G<"difflib.IS_CHARACTER_JUNK">()) }, [](arguments a) -> py
                {
                    py a_ = a[0];
                    py b = a[1];
                    py linejunk = a[2];
                    py charjunk = a[3];
                    return (G<"difflib.Differ">())(linejunk, charjunk).method("compare", { a_, b });
                }));

            define("difflib", "_mdiff", w.function("_mdiff", "_mdiff", { py_positional("fromlines"), py_positional("tolines"), py_positional("context", none()), py_positional("linejunk", none()), py_positional("charjunk", G<"difflib.IS_CHARACTER_JUNK">()) }, [](arguments a) -> py
                {
                    return make_native_generator("_mdiff", "_mdiff", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                    {
                        auto const fromlines = std::make_shared<py>(a[0]);
                        auto const tolines = std::make_shared<py>(a[1]);
                        py context = a[2];
                        py linejunk = a[3];
                        py charjunk = a[4];
                        auto const _line_iterator = std::make_shared<py>();
                        py _line_pair_iterator;
                        auto const _make_line = std::make_shared<py>();
                        auto const change_re = std::make_shared<py>();
                        py contextLines;
                        auto const diff_lines_iterator = std::make_shared<py>();
                        auto const found_diff = std::make_shared<py>();
                        auto const from_line = std::make_shared<py>();
                        py i;
                        py index;
                        py line_pair_iterator;
                        py lines_to_write;
                        py re;
                        auto const to_line = std::make_shared<py>();
                        re = module_of("re");
                        // regular expression for finding intraline change indices
                        *change_re = re.method("compile", { S<"(\\++|\\-+|\\^+)">() });
                        // create the difference iterator to generate the differences
                        *diff_lines_iterator = (G<"difflib.ndiff">())((*fromlines), (*tolines), linejunk, charjunk);
                        *_make_line = w.function("_make_line", "_mdiff.<locals>._make_line", { py_positional("lines"), py_positional("format_key"), py_positional("side"), py_positional("num_lines", py_list({ I<0>(), I<0>() })) }, [change_re](arguments a) -> py
    {
        py lines = a[0];
        py format_key = a[1];
        py side = a[2];
        py num_lines = a[3];
        py begin;
        py end;
        py key;
        py markers;
        py record_sub_info;
        py sub_info;
        py text;
        {
            py const target94 = num_lines;
            py const key94 = side;
            target94.set_item(key94, inplace(NEOS_PY_ADD, target94[key94], I<1>()));
        }
        // Handle case where no user markup is to be added, just return line of
        // text with user's line format to allow for usage of the line number.
        if (format_key.is_none())
        {
            return py_tuple({ num_lines[side], lines.method("pop", { I<0>() })[B<"slice">()(I<2>(), none(), none())] });
        }
        // Handle case of intraline changes
        if (format_key == (S<"?">()))
        {
            auto const items95 = unpack(py_tuple({ lines.method("pop", { I<0>() }), lines.method("pop", { I<0>() }) }), 2);
            text = items95[0];
            markers = items95[1];
            // find intraline changes (store change type and indices in tuples)
            sub_info = py_list();
            record_sub_info = w.function("record_sub_info", "_mdiff.<locals>._make_line.<locals>.record_sub_info", { py_positional("match_object"), py_positional("sub_info", sub_info) }, [](arguments a) -> py
        {
            py match_object = a[0];
            py sub_info = a[1];
            sub_info.method("append", { py_list({ match_object.method("group", { I<1>() })[I<0>()], match_object.method("span") }) });
            return match_object.method("group", { I<1>() });
        });
            (*change_re).method("sub", { record_sub_info, markers });
            // process each tuple inserting our special marks that won't be
            // noticed by an xml/html escaper.
            for (py const& item : iterate{ (B<"reversed">())(sub_info) })
            {
                auto const items97 = unpack(item, 2);
                key = items97[0];
                auto const items98 = unpack(items97[1], 2);
                begin = items98[0];
                end = items98[1];
                text = ((((text[B<"slice">()(I<0>(), begin, none())] + (S<"\u0000">())) + key) + text[B<"slice">()(begin, end, none())]) + (S<"\u0001">())) + text[B<"slice">()(end, none(), none())];
            }
            text = text[B<"slice">()(I<2>(), none(), none())];
        }
        else
        {
            // Handle case of add/delete entire line
            text = lines.method("pop", { I<0>() })[B<"slice">()(I<2>(), none(), none())];
            // if line of text is just a newline, insert a space so there is
            // something for the user to highlight and see.
            if (!text.truthy())
            {
                text = S<" ">();
            }
            // insert marks that won't be noticed by an xml/html escaper.
            text = (((S<"\u0000">()) + format_key) + text) + (S<"\u0001">());
        }
        // Return line of text, first allow user's line formatter to do its
        // thing (such as adding the line number) then replace the special
        // marks with what the user's change markup.
        return py_tuple({ num_lines[side], text });
    });
                        *_line_iterator = w.function("_line_iterator", "_mdiff.<locals>._line_iterator", {  }, [_make_line, diff_lines_iterator](arguments a) -> py
    {
        return make_native_generator("_line_iterator", "_mdiff.<locals>._line_iterator", w.module, [a = std::vector<py>(a.begin(), a.end()), _make_line, diff_lines_iterator]() -> py
        {
            py from_line;
            py lines;
            py num_blanks_pending;
            py num_blanks_to_yield;
            py s;
            py to_line;
            lines = py_list();
            auto const items99 = unpack(py_tuple({ I<0>(), I<0>() }), 2);
            num_blanks_pending = items99[0];
            num_blanks_to_yield = items99[1];
            while (boolean(true).truthy())
            {
                // Load up next 4 lines so we can look ahead, create strings which
                // are a concatenation of the first character of each of the 4 lines
                // so we can do some very readable comparisons.
                while (py{ lines.size() } < (I<4>()))
                {
                    lines.method("append", { (B<"next">())((*diff_lines_iterator), S<"X">()) });
                }
                s = (S<"">()).method("join", { [&]() -> py { py const comprehension102 = py_list(); py line; for (py const& item : iterate{ lines }) { line = item; comprehension102.method("append", { line[I<0>()] }); } return comprehension102; }() });
                if (s.method("startswith", { S<"X">() }).truthy())
                {
                    // When no more lines, pump out any remaining blank lines so the
                    // corresponding add/delete lines get a matching blank line so
                    // all line pairs get yielded at the next level.
                    num_blanks_to_yield = num_blanks_pending;
                }
                else if (s.method("startswith", { S<"-?+?">() }).truthy())
                {
                    // simple intraline change
                    py_yield(py_tuple({ (*_make_line)(lines, S<"?">(), I<0>()), (*_make_line)(lines, S<"?">(), I<1>()), boolean(true) }));
                    continue;
                }
                else if (s.method("startswith", { S<"--++">() }).truthy())
                {
                    // in delete block, add block coming: we do NOT want to get
                    // caught up on blank lines yet, just process the delete line
                    num_blanks_pending = inplace(NEOS_PY_SUBTRACT, num_blanks_pending, I<1>());
                    py_yield(py_tuple({ (*_make_line)(lines, S<"-">(), I<0>()), none(), boolean(true) }));
                    continue;
                }
                else if (s.method("startswith", { py_tuple({ S<"--?+">(), S<"--+">(), S<"- ">() }) }).truthy())
                {
                    // in delete block and see an intraline change or unchanged line
                    // coming: yield the delete line and then blanks
                    auto const items103 = unpack(py_tuple({ (*_make_line)(lines, S<"-">(), I<0>()), none() }), 2);
                    from_line = items103[0];
                    to_line = items103[1];
                    auto const items104 = unpack(py_tuple({ num_blanks_pending - (I<1>()), I<0>() }), 2);
                    num_blanks_to_yield = items104[0];
                    num_blanks_pending = items104[1];
                }
                else if (s.method("startswith", { S<"-+?">() }).truthy())
                {
                    // intraline change
                    py_yield(py_tuple({ (*_make_line)(lines, none(), I<0>()), (*_make_line)(lines, S<"?">(), I<1>()), boolean(true) }));
                    continue;
                }
                else if (s.method("startswith", { S<"-?+">() }).truthy())
                {
                    // intraline change
                    py_yield(py_tuple({ (*_make_line)(lines, S<"?">(), I<0>()), (*_make_line)(lines, none(), I<1>()), boolean(true) }));
                    continue;
                }
                else if (s.method("startswith", { S<"-">() }).truthy())
                {
                    // delete FROM line
                    num_blanks_pending = inplace(NEOS_PY_SUBTRACT, num_blanks_pending, I<1>());
                    py_yield(py_tuple({ (*_make_line)(lines, S<"-">(), I<0>()), none(), boolean(true) }));
                    continue;
                }
                else if (s.method("startswith", { S<"+--">() }).truthy())
                {
                    // in add block, delete block coming: we do NOT want to get
                    // caught up on blank lines yet, just process the add line
                    num_blanks_pending = inplace(NEOS_PY_ADD, num_blanks_pending, I<1>());
                    py_yield(py_tuple({ none(), (*_make_line)(lines, S<"+">(), I<1>()), boolean(true) }));
                    continue;
                }
                else if (s.method("startswith", { py_tuple({ S<"+ ">(), S<"+-">() }) }).truthy())
                {
                    // will be leaving an add block: yield blanks then add line
                    auto const items105 = unpack(py_tuple({ none(), (*_make_line)(lines, S<"+">(), I<1>()) }), 2);
                    from_line = items105[0];
                    to_line = items105[1];
                    auto const items106 = unpack(py_tuple({ num_blanks_pending + (I<1>()), I<0>() }), 2);
                    num_blanks_to_yield = items106[0];
                    num_blanks_pending = items106[1];
                }
                else if (s.method("startswith", { S<"+">() }).truthy())
                {
                    // inside an add block, yield the add line
                    num_blanks_pending = inplace(NEOS_PY_ADD, num_blanks_pending, I<1>());
                    py_yield(py_tuple({ none(), (*_make_line)(lines, S<"+">(), I<1>()), boolean(true) }));
                    continue;
                }
                else if (s.method("startswith", { S<" ">() }).truthy())
                {
                    // unchanged text, yield it to both sides
                    py_yield(py_tuple({ (*_make_line)(lines[B<"slice">()(none(), none(), none())], none(), I<0>()), (*_make_line)(lines, none(), I<1>()), boolean(false) }));
                    continue;
                }
                // Catch up on the blank lines so when we yield the next from/to
                // pair, they are lined up.
                while (num_blanks_to_yield < (I<0>()))
                {
                    num_blanks_to_yield = inplace(NEOS_PY_ADD, num_blanks_to_yield, I<1>());
                    py_yield(py_tuple({ none(), py_tuple({ S<"">(), S<"\n">() }), boolean(true) }));
                }
                while (num_blanks_to_yield > (I<0>()))
                {
                    num_blanks_to_yield = inplace(NEOS_PY_SUBTRACT, num_blanks_to_yield, I<1>());
                    py_yield(py_tuple({ py_tuple({ S<"">(), S<"\n">() }), none(), boolean(true) }));
                }
                if (s.method("startswith", { S<"X">() }).truthy())
                {
                    return none();
                }
                else
                {
                    py_yield(py_tuple({ from_line, to_line, boolean(true) }));
                }
            }
            return none();
        });
    });
                        _line_pair_iterator = w.function("_line_pair_iterator", "_mdiff.<locals>._line_pair_iterator", {  }, [_line_iterator](arguments a) -> py
    {
        return make_native_generator("_line_pair_iterator", "_mdiff.<locals>._line_pair_iterator", w.module, [a = std::vector<py>(a.begin(), a.end()), _line_iterator]() -> py
        {
            py found_diff;
            py fromDiff;
            py from_line;
            py fromlines;
            py line_iterator;
            py to_diff;
            py to_line;
            py tolines;
            line_iterator = (*_line_iterator)();
            auto const items109 = unpack(py_tuple({ py_list(), py_list() }), 2);
            fromlines = items109[0];
            tolines = items109[1];
            while (boolean(true).truthy())
            {
                // Collecting lines of text until we have a from/to pair
                while (((py{ fromlines.size() } == (I<0>())) || (py{ tolines.size() } == (I<0>()))))
                {
                    try
                    {
                        auto const items113 = unpack((B<"next">())(line_iterator), 3);
                        from_line = items113[0];
                        to_line = items113[1];
                        found_diff = items113[2];
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler112;
                        if (matches(handler112.exception, B<"StopIteration">()))
                        {
                            return none();
                        }
                        else
                            py_reraise(handler112.exception);
                    }
                    if (!from_line.is_none())
                    {
                        fromlines.method("append", { py_tuple({ from_line, found_diff }) });
                    }
                    if (!to_line.is_none())
                    {
                        tolines.method("append", { py_tuple({ to_line, found_diff }) });
                    }
                }
                // Once we have a pair, remove them from the collection and yield it
                auto const items114 = unpack(fromlines.method("pop", { I<0>() }), 2);
                from_line = items114[0];
                fromDiff = items114[1];
                auto const items115 = unpack(tolines.method("pop", { I<0>() }), 2);
                to_line = items115[0];
                to_diff = items115[1];
                py_yield(py_tuple({ from_line, to_line, [&]() -> py { py const t = fromDiff; return t.truthy() ? t : to_diff; }() }));
            }
            return none();
        });
    });
                        // Handle case where user does not want context differencing, just yield
                        // them up without doing anything else with them.
                        line_pair_iterator = _line_pair_iterator();
                        if (context.is_none())
                        {
                            yield_from(line_pair_iterator);
                        }
                        else
                        {
                            // Handle case where user wants context differencing.  We must do some
                            // storage of lines until we know for sure that they are to be yielded.
                            context = inplace(NEOS_PY_ADD, context, I<1>());
                            lines_to_write = I<0>();
                            while (boolean(true).truthy())
                            {
                                // Store lines up until we find a difference, note use of a
                                // circular queue because we only need to keep around what
                                // we need for context.
                                auto const items117 = unpack(py_tuple({ I<0>(), py_list({ none() }) * context }), 2);
                                index = items117[0];
                                contextLines = items117[1];
                                *found_diff = boolean(false);
                                while ((*found_diff).is(boolean(false)))
                                {
                                    try
                                    {
                                        auto const items120 = unpack((B<"next">())(line_pair_iterator), 3);
                                        *from_line = items120[0];
                                        *to_line = items120[1];
                                        *found_diff = items120[2];
                                    }
                                    catch (raised const&)
                                    {
                                        pyclass::handler_scope handler119;
                                        if (matches(handler119.exception, B<"StopIteration">()))
                                        {
                                            return none();
                                        }
                                        else
                                            py_reraise(handler119.exception);
                                    }
                                    i = index % context;
                                    contextLines.set_item(i, py_tuple({ (*from_line), (*to_line), (*found_diff) }));
                                    index = inplace(NEOS_PY_ADD, index, I<1>());
                                }
                                // Yield lines that we have collected so far, but first yield
                                // the user's separator.
                                if (index > context)
                                {
                                    py_yield(py_tuple({ none(), none(), none() }));
                                    lines_to_write = context;
                                }
                                else
                                {
                                    lines_to_write = index;
                                    index = I<0>();
                                }
                                while (lines_to_write.truthy())
                                {
                                    i = index % context;
                                    index = inplace(NEOS_PY_ADD, index, I<1>());
                                    py_yield(contextLines[i]);
                                    lines_to_write = inplace(NEOS_PY_SUBTRACT, lines_to_write, I<1>());
                                }
                                // Now yield the context lines after the change
                                lines_to_write = context - (I<1>());
                                try
                                {
                                    while (lines_to_write.truthy())
                                    {
                                        auto const items124 = unpack((B<"next">())(line_pair_iterator), 3);
                                        *from_line = items124[0];
                                        *to_line = items124[1];
                                        *found_diff = items124[2];
                                        // If another change within the context, extend the context
                                        if ((*found_diff).truthy())
                                        {
                                            lines_to_write = context - (I<1>());
                                        }
                                        else
                                        {
                                            lines_to_write = inplace(NEOS_PY_SUBTRACT, lines_to_write, I<1>());
                                        }
                                        py_yield(py_tuple({ (*from_line), (*to_line), (*found_diff) }));
                                    }
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler122;
                                    if (matches(handler122.exception, B<"StopIteration">()))
                                    {
                                        // Catch exception from next() and return normally
                                        return none();
                                    }
                                    else
                                        py_reraise(handler122.exception);
                                }
                            }
                        }
                        return none();
                    });
                }));

            define("difflib", "_file_template", S<"\n<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Transitional//EN\"\n          \"http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd\">\n\n<html>\n\n<head>\n    <meta http-equiv=\"Content-Type\"\n          content=\"text/html; charset=%(charset)s\" />\n    <title></title>\n    <style type=\"text/css\">%(styles)s\n    </style>\n</head>\n\n<body>\n    %(table)s%(legend)s\n</body>\n\n</html>">());
            define("difflib", "_styles", S<"\n        table.diff {font-family: Menlo, Consolas, Monaco, Liberation Mono, Lucida Console, monospace; border:medium}\n        .diff_header {background-color:#e0e0e0}\n        td.diff_header {text-align:right}\n        .diff_next {background-color:#c0c0c0}\n        .diff_add {background-color:#aaffaa}\n        .diff_chg {background-color:#ffff77}\n        .diff_sub {background-color:#ffaaaa}">());
            define("difflib", "_table_template", S<"\n    <table class=\"diff\" id=\"difflib_chg_%(prefix)s_top\"\n           cellspacing=\"0\" cellpadding=\"0\" rules=\"groups\" >\n        <colgroup></colgroup> <colgroup></colgroup> <colgroup></colgroup>\n        <colgroup></colgroup> <colgroup></colgroup> <colgroup></colgroup>\n        %(header_row)s\n        <tbody>\n%(data_rows)s        </tbody>\n    </table>">());
            define("difflib", "_legend", S<"\n    <table class=\"diff\" summary=\"Legends\">\n        <tr> <th colspan=\"2\"> Legends </th> </tr>\n        <tr> <td> <table border=\"\" summary=\"Colors\">\n                      <tr><th> Colors </th> </tr>\n                      <tr><td class=\"diff_add\">&nbsp;Added&nbsp;</td></tr>\n                      <tr><td class=\"diff_chg\">Changed</td> </tr>\n                      <tr><td class=\"diff_sub\">Deleted</td> </tr>\n                  </table></td>\n             <td> <table border=\"\" summary=\"Links\">\n                      <tr><th colspan=\"2\"> Links </th> </tr>\n                      <tr><td>(f)irst change</td> </tr>\n                      <tr><td>(n)ext change</td> </tr>\n                      <tr><td>(t)op</td> </tr>\n                  </table></td> </tr>\n    </table>">());
            define("difflib", "HtmlDiff", w.make_class("HtmlDiff", { B<"object">() }, {
                { "_file_template", G<"difflib._file_template">() },
                { "_styles", G<"difflib._styles">() },
                { "_table_template", G<"difflib._table_template">() },
                { "_legend", G<"difflib._legend">() },
                { "_default_prefix", I<0>() },
                { "__init__", w.method("HtmlDiff", "__init__", { py_positional("tabsize", I<8>()), py_positional("wrapcolumn", none()), py_positional("linejunk", none()), py_positional("charjunk", G<"difflib.IS_CHARACTER_JUNK">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py tabsize = a[1];
                        py wrapcolumn = a[2];
                        py linejunk = a[3];
                        py charjunk = a[4];
                        self.set_attr("_tabsize", tabsize);
                        self.set_attr("_wrapcolumn", wrapcolumn);
                        self.set_attr("_linejunk", linejunk);
                        self.set_attr("_charjunk", charjunk);
                        return none();
                    }) },
                { "make_file", w.method("HtmlDiff", "make_file", { py_positional("fromlines"), py_positional("tolines"), py_positional("fromdesc", S<"">()), py_positional("todesc", S<"">()), py_positional("context", boolean(false)), py_positional("numlines", I<5>()), py_keyword("charset", S<"utf-8">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fromlines = a[1];
                        py tolines = a[2];
                        py fromdesc = a[3];
                        py todesc = a[4];
                        py context = a[5];
                        py numlines = a[6];
                        py charset = a[7];
                        return (self.attr("_file_template") % (B<"dict">()).call({  }, { { "styles", self.attr("_styles") }, { "legend", self.attr("_legend") }, { "table", self.method("make_table", { fromlines, tolines, fromdesc, todesc }, { { "context", context }, { "numlines", numlines } }) }, { "charset", charset } })).method("encode", { charset, S<"xmlcharrefreplace">() }).method("decode", { charset });
                    }) },
                { "_tab_newline_replace", w.method("HtmlDiff", "_tab_newline_replace", { py_positional("fromlines"), py_positional("tolines") }, [](arguments a) -> py
                    {
                        auto const self = std::make_shared<py>(a[0]);
                        py fromlines = a[1];
                        py tolines = a[2];
                        py expand_tabs;
                        expand_tabs = w.function("expand_tabs", "HtmlDiff._tab_newline_replace.<locals>.expand_tabs", { py_positional("line") }, [self](arguments a) -> py
    {
        py line = a[0];
        line = line.method("replace", { S<" ">(), S<"\u0000">() });
        // expand tabs into spaces
        line = line.method("expandtabs", { (*self).attr("_tabsize") });
        // replace spaces from expanded tabs back into tab characters
        // (we'll replace them with markup after we do differencing)
        line = line.method("replace", { S<" ">(), S<"\t">() });
        return line.method("replace", { S<"\u0000">(), S<" ">() }).method("rstrip", { S<"\n">() });
    });
                        fromlines = [&]() -> py { py const comprehension125 = py_list(); py line; for (py const& item : iterate{ fromlines }) { line = item; comprehension125.method("append", { expand_tabs(line) }); } return comprehension125; }();
                        tolines = [&]() -> py { py const comprehension126 = py_list(); py line; for (py const& item : iterate{ tolines }) { line = item; comprehension126.method("append", { expand_tabs(line) }); } return comprehension126; }();
                        return py_tuple({ fromlines, tolines });
                    }) },
                { "_split_line", w.method("HtmlDiff", "_split_line", { py_positional("data_list"), py_positional("line_num"), py_positional("text") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data_list = a[1];
                        py line_num = a[2];
                        py text = a[3];
                        py i;
                        py line1;
                        py line2;
                        py mark;
                        py max_;
                        py n;
                        py size;
                        // if blank line or context separator, just add it to the output list
                        if (!line_num.truthy())
                        {
                            data_list.method("append", { py_tuple({ line_num, text }) });
                            return none();
                        }
                        // if line text doesn't need wrapping, just add it to the output list
                        size = py{ text.size() };
                        max_ = self.attr("_wrapcolumn");
                        if (((size <= max_) || ((size - (text.method("count", { S<"\u0000">() }) * (I<3>()))) <= max_)))
                        {
                            data_list.method("append", { py_tuple({ line_num, text }) });
                            return none();
                        }
                        // scan text looking for the wrap point, keeping track if the wrap
                        // point is inside markers
                        i = I<0>();
                        n = I<0>();
                        mark = S<"">();
                        while (((n < max_) && (i < size)))
                        {
                            if (text[i] == (S<"\u0000">()))
                            {
                                i = inplace(NEOS_PY_ADD, i, I<1>());
                                mark = text[i];
                                i = inplace(NEOS_PY_ADD, i, I<1>());
                            }
                            else if (text[i] == (S<"\u0001">()))
                            {
                                i = inplace(NEOS_PY_ADD, i, I<1>());
                                mark = S<"">();
                            }
                            else
                            {
                                i = inplace(NEOS_PY_ADD, i, I<1>());
                                n = inplace(NEOS_PY_ADD, n, I<1>());
                            }
                        }
                        // wrap point is inside text, break it up into separate lines
                        line1 = text[B<"slice">()(none(), i, none())];
                        line2 = text[B<"slice">()(i, none(), none())];
                        // if wrap point is inside markers, place end marker at end of first
                        // line and start marker at beginning of second line because each
                        // line will have its own table tag markup around it.
                        if (mark.truthy())
                        {
                            line1 = line1 + (S<"\u0001">());
                            line2 = ((S<"\u0000">()) + mark) + line2;
                        }
                        // tack on first line onto the output list
                        data_list.method("append", { py_tuple({ line_num, line1 }) });
                        // use this routine again to wrap the remaining text
                        self.method("_split_line", { data_list, S<">">(), line2 });
                        return none();
                    }) },
                { "_line_wrapper", w.method("HtmlDiff", "_line_wrapper", { py_positional("diffs") }, [](arguments a) -> py
                    {
                        return make_native_generator("_line_wrapper", "HtmlDiff._line_wrapper", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                        {
                            py self = a[0];
                            py diffs = a[1];
                            py flag;
                            py fromdata;
                            py fromline;
                            py fromlist;
                            py fromtext;
                            py todata;
                            py toline;
                            py tolist;
                            py totext;
                            // pull from/to data and flags from mdiff iterator
                            for (py const& item : iterate{ diffs })
                            {
                                auto const items129 = unpack(item, 3);
                                fromdata = items129[0];
                                todata = items129[1];
                                flag = items129[2];
                                // check for context separators and pass them through
                                if (flag.is_none())
                                {
                                    py_yield(py_tuple({ fromdata, todata, flag }));
                                    continue;
                                }
                                auto const items130 = unpack(py_tuple({ fromdata, todata }), 2);
                                auto const items131 = unpack(items130[0], 2);
                                fromline = items131[0];
                                fromtext = items131[1];
                                auto const items132 = unpack(items130[1], 2);
                                toline = items132[0];
                                totext = items132[1];
                                // for each from/to line split it at the wrap column to form
                                // list of text lines.
                                auto const items133 = unpack(py_tuple({ py_list(), py_list() }), 2);
                                fromlist = items133[0];
                                tolist = items133[1];
                                self.method("_split_line", { fromlist, fromline, fromtext });
                                self.method("_split_line", { tolist, toline, totext });
                                // yield from/to line in pairs inserting blank lines as
                                // necessary when one side has more wrapped lines
                                while ((fromlist.truthy() || tolist.truthy()))
                                {
                                    if (fromlist.truthy())
                                    {
                                        fromdata = fromlist.method("pop", { I<0>() });
                                    }
                                    else
                                    {
                                        fromdata = py_tuple({ S<"">(), S<" ">() });
                                    }
                                    if (tolist.truthy())
                                    {
                                        todata = tolist.method("pop", { I<0>() });
                                    }
                                    else
                                    {
                                        todata = py_tuple({ S<"">(), S<" ">() });
                                    }
                                    py_yield(py_tuple({ fromdata, todata, flag }));
                                }
                            }
                            return none();
                        });
                    }) },
                { "_collect_lines", w.method("HtmlDiff", "_collect_lines", { py_positional("diffs") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py diffs = a[1];
                        py flag;
                        py flaglist;
                        py fromdata;
                        py fromlist;
                        py todata;
                        py tolist;
                        auto const items135 = unpack(py_tuple({ py_list(), py_list(), py_list() }), 3);
                        fromlist = items135[0];
                        tolist = items135[1];
                        flaglist = items135[2];
                        // pull from/to data and flags from mdiff style iterator
                        for (py const& item : iterate{ diffs })
                        {
                            auto const items137 = unpack(item, 3);
                            fromdata = items137[0];
                            todata = items137[1];
                            flag = items137[2];
                            try
                            {
                                // store HTML markup of the lines into the lists
                                fromlist.method("append", { call_ex(self.attr("_format_line"), { I<0>(), flag }, fromdata, py{}) });
                                tolist.method("append", { call_ex(self.attr("_format_line"), { I<1>(), flag }, todata, py{}) });
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler138;
                                if (matches(handler138.exception, B<"TypeError">()))
                                {
                                    // exceptions occur for lines where context separators go
                                    fromlist.method("append", { none() });
                                    tolist.method("append", { none() });
                                }
                                else
                                    py_reraise(handler138.exception);
                            }
                            flaglist.method("append", { flag });
                        }
                        return py_tuple({ fromlist, tolist, flaglist });
                    }) },
                { "_format_line", w.method("HtmlDiff", "_format_line", { py_positional("side"), py_positional("flag"), py_positional("linenum"), py_positional("text") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py side = a[1];
                        py flag = a[2];
                        py linenum = a[3];
                        py text = a[4];
                        py id;
                        try
                        {
                            linenum = (S<"%d">()) % linenum;
                            id = (S<" id=\"%s%s\"">()) % py_tuple({ self.attr("_prefix")[side], linenum });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler139;
                            if (matches(handler139.exception, B<"TypeError">()))
                            {
                                // handle blank lines where linenum is '>' or ''
                                id = S<"">();
                            }
                            else
                                py_reraise(handler139.exception);
                        }
                        // replace those things that would get confused with HTML symbols
                        text = text.method("replace", { S<"&">(), S<"&amp;">() }).method("replace", { S<">">(), S<"&gt;">() }).method("replace", { S<"<">(), S<"&lt;">() });
                        // make space non-breakable so they don't get compressed or line wrapped
                        text = text.method("replace", { S<" ">(), S<"&nbsp;">() }).method("rstrip");
                        return (S<"<td class=\"diff_header\"%s>%s</td><td nowrap=\"nowrap\">%s</td>">()) % py_tuple({ id, linenum, text });
                    }) },
                { "_make_prefix", w.method("HtmlDiff", "_make_prefix", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fromprefix;
                        py toprefix;
                        // Generate a unique anchor prefix so multiple tables
                        // can exist on the same HTML page without conflicts.
                        fromprefix = (S<"from%d_">()) % (G<"difflib.HtmlDiff">()).attr("_default_prefix");
                        toprefix = (S<"to%d_">()) % (G<"difflib.HtmlDiff">()).attr("_default_prefix");
                        {
                            py const target140 = G<"difflib.HtmlDiff">();
                            target140.set_attr("_default_prefix", inplace(NEOS_PY_ADD, target140.attr("_default_prefix"), I<1>()));
                        }
                        // store prefixes so line format method has access
                        self.set_attr("_prefix", py_list({ fromprefix, toprefix }));
                        return none();
                    }) },
                { "_convert_flags", w.method("HtmlDiff", "_convert_flags", { py_positional("fromlist"), py_positional("tolist"), py_positional("flaglist"), py_positional("context"), py_positional("numlines") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fromlist = a[1];
                        py tolist = a[2];
                        py flaglist = a[3];
                        py context = a[4];
                        py numlines = a[5];
                        py flag;
                        py i;
                        py in_change;
                        py last;
                        py next_href;
                        py next_id;
                        py num_chg;
                        py toprefix;
                        // all anchor names will be generated using the unique "to" prefix
                        toprefix = self.attr("_prefix")[I<1>()];
                        // process change flags, generating middle column of next anchors/links
                        next_id = py_list({ S<"">() }) * py{ flaglist.size() };
                        next_href = py_list({ S<"">() }) * py{ flaglist.size() };
                        auto const items141 = unpack(py_tuple({ I<0>(), boolean(false) }), 2);
                        num_chg = items141[0];
                        in_change = items141[1];
                        last = I<0>();
                        for (py const& item : iterate{ (B<"enumerate">())(flaglist) })
                        {
                            auto const items143 = unpack(item, 2);
                            i = items143[0];
                            flag = items143[1];
                            if (flag.truthy())
                            {
                                if (!in_change.truthy())
                                {
                                    in_change = boolean(true);
                                    last = i;
                                    // at the beginning of a change, drop an anchor a few lines
                                    // (the context lines) before the change for the previous
                                    // link
                                    i = (B<"max">())(py_list({ I<0>(), i - numlines }));
                                    next_id.set_item(i, (S<" id=\"difflib_chg_%s_%d\"">()) % py_tuple({ toprefix, num_chg }));
                                    // at the beginning of a change, drop a link to the next
                                    // change
                                    num_chg = inplace(NEOS_PY_ADD, num_chg, I<1>());
                                    next_href.set_item(last, (S<"<a href=\"#difflib_chg_%s_%d\">n</a>">()) % py_tuple({ toprefix, num_chg }));
                                }
                            }
                            else
                            {
                                in_change = boolean(false);
                            }
                        }
                        // check for cases where there is no content to avoid exceptions
                        if (!flaglist.truthy())
                        {
                            flaglist = py_list({ boolean(false) });
                            next_id = py_list({ S<"">() });
                            next_href = py_list({ S<"">() });
                            last = I<0>();
                            if (context.truthy())
                            {
                                fromlist = py_list({ S<"<td></td><td>&nbsp;No Differences Found&nbsp;</td>">() });
                                tolist = fromlist;
                            }
                            else
                            {
                                py const value144 = py_list({ S<"<td></td><td>&nbsp;Empty File&nbsp;</td>">() });
                                fromlist = value144;
                                tolist = value144;
                            }
                        }
                        // if not a change on first line, drop a link
                        if (!flaglist[I<0>()].truthy())
                        {
                            next_href.set_item(I<0>(), (S<"<a href=\"#difflib_chg_%s_0\">f</a>">()) % toprefix);
                        }
                        // redo the last link to link to the top
                        next_href.set_item(last, (S<"<a href=\"#difflib_chg_%s_top\">t</a>">()) % toprefix);
                        return py_tuple({ fromlist, tolist, flaglist, next_href, next_id });
                    }) },
                { "make_table", w.method("HtmlDiff", "make_table", { py_positional("fromlines"), py_positional("tolines"), py_positional("fromdesc", S<"">()), py_positional("todesc", S<"">()), py_positional("context", boolean(false)), py_positional("numlines", I<5>()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fromlines = a[1];
                        py tolines = a[2];
                        py fromdesc = a[3];
                        py todesc = a[4];
                        py context = a[5];
                        py numlines = a[6];
                        py context_lines;
                        py diffs;
                        py flaglist;
                        py fmt;
                        py fromlist;
                        py header_row;
                        py i;
                        py next_href;
                        py next_id;
                        py s;
                        py table;
                        py tolist;
                        // make unique anchor prefixes so that multiple tables may exist
                        // on the same page without conflict.
                        self.method("_make_prefix");
                        // change tabs to spaces before it gets more difficult after we insert
                        // markup
                        // it also removes trailing newlines, causing some diffs to be missed
                        // see: gh-71896
                        auto const items145 = unpack(self.method("_tab_newline_replace", { fromlines, tolines }), 2);
                        fromlines = items145[0];
                        tolines = items145[1];
                        // create diffs iterator which generates side by side from/to data
                        if (context.truthy())
                        {
                            context_lines = numlines;
                        }
                        else
                        {
                            context_lines = none();
                        }
                        diffs = (G<"difflib._mdiff">()).call({ fromlines, tolines, context_lines }, { { "linejunk", self.attr("_linejunk") }, { "charjunk", self.attr("_charjunk") } });
                        // set up iterator to wrap lines that exceed desired width
                        if (self.attr("_wrapcolumn").truthy())
                        {
                            diffs = self.method("_line_wrapper", { diffs });
                        }
                        // collect up from/to lines and flags into lists (also format the lines)
                        auto const items146 = unpack(self.method("_collect_lines", { diffs }), 3);
                        fromlist = items146[0];
                        tolist = items146[1];
                        flaglist = items146[2];
                        // process change flags, generating middle column of next anchors/links
                        auto const items147 = unpack(self.method("_convert_flags", { fromlist, tolist, flaglist, context, numlines }), 5);
                        fromlist = items147[0];
                        tolist = items147[1];
                        flaglist = items147[2];
                        next_href = items147[3];
                        next_id = items147[4];
                        s = py_list();
                        fmt = (S<"            <tr><td class=\"diff_next\"%s>%s</td>%s">()) + (S<"<td class=\"diff_next\">%s</td>%s</tr>\n">());
                        for (py const& item : iterate{ (B<"range">())(py{ flaglist.size() }) })
                        {
                            i = item;
                            if (flaglist[i].is_none())
                            {
                                // mdiff yields None on separator lines skip the bogus ones
                                // generated for the first line
                                if (i > (I<0>()))
                                {
                                    s.method("append", { S<"        </tbody>        \n        <tbody>\n">() });
                                }
                            }
                            else
                            {
                                s.method("append", { fmt % py_tuple({ next_id[i], next_href[i], fromlist[i], next_href[i], tolist[i] }) });
                            }
                        }
                        if ((fromdesc.truthy() || todesc.truthy()))
                        {
                            header_row = (S<"<thead><tr>%s%s%s%s</tr></thead>">()) % py_tuple({ S<"<th class=\"diff_next\"><br /></th>">(), (S<"<th colspan=\"2\" class=\"diff_header\">%s</th>">()) % fromdesc, S<"<th class=\"diff_next\"><br /></th>">(), (S<"<th colspan=\"2\" class=\"diff_header\">%s</th>">()) % todesc });
                        }
                        else
                        {
                            header_row = S<"">();
                        }
                        table = self.attr("_table_template") % (B<"dict">()).call({  }, { { "data_rows", (S<"">()).method("join", { s }) }, { "header_row", header_row }, { "prefix", self.attr("_prefix")[I<1>()] } });
                        return table.method("replace", { S<"\u0000+">(), S<"<span class=\"diff_add\">">() }).method("replace", { S<"\u0000-">(), S<"<span class=\"diff_sub\">">() }).method("replace", { S<"\u0000^">(), S<"<span class=\"diff_chg\">">() }).method("replace", { S<"\u0001">(), S<"</span>">() }).method("replace", { S<"\t">(), S<"&nbsp;">() });
                    }) }, }));

            undefine("difflib", "re");
            define("difflib", "restore", w.function("restore", "restore", { py_positional("delta"), py_positional("which") }, [](arguments a) -> py
                {
                    return make_native_generator("restore", "restore", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                    {
                        py delta = a[0];
                        py which = a[1];
                        py line;
                        py prefixes;
                        py tag;
                        try
                        {
                            tag = dict_of({ { I<1>(), S<"- ">() }, { I<2>(), S<"+ ">() } })[(B<"int">())(which)];
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler149;
                            if (matches(handler149.exception, B<"KeyError">()))
                            {
                                fn_raise_from((B<"ValueError">())((S<"unknown delta choice (must be 1 or 2): %r">()) % which).get(), none().get());
                                throw raised{};
                            }
                            else
                                py_reraise(handler149.exception);
                        }
                        prefixes = py_tuple({ S<"  ">(), tag });
                        for (py const& item : iterate{ delta })
                        {
                            line = item;
                            if (prefixes.contains(line[B<"slice">()(none(), I<2>(), none())]))
                            {
                                py_yield(line[B<"slice">()(I<2>(), none(), none())]);
                            }
                        }
                        return none();
                    });
                }));

            define("difflib", "_test", w.function("_test", "_test", {  }, [](arguments a) -> py
                {
                    py difflib;
                    py doctest;
                    doctest = module_of("doctest");
                    difflib = module_of("difflib");
                    return doctest.method("testmod", { difflib });
                }));

            // (if __name__ == "__main__": the module is not run as the program)
        }

        bool difflib_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_difflib();
                }
                if (auto const doc = difflib_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("_nlargest", import_attribute("heapq", "nlargest").release());
                aAttributes.emplace_back("_namedtuple", import_attribute("collections", "namedtuple").release());
                aAttributes.emplace_back("GenericAlias", import_attribute("types", "GenericAlias").release());
                aAttributes.emplace_back("re", py{ fn_import("re") }.release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("difflib", difflib_module);
    }
}
