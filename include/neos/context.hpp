/*
  context.hpp

  Copyright (c) 2019 Leigh Johnston.  All Rights Reserved.

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
*/

#include <neolib/neolib.hpp>
#include <neolib/json.hpp>

namespace neos
{
	class context
	{
	public:
		context();
	public:
		bool schema_loaded() const;
		void load_schema(const std::string& aSchemaPath);
		const std::string& language() const;
	public:
		bool running() const;
		void run();
	private:
		std::optional<neolib::rjson> iSchema;
		std::string iLanguage;
		bool iRunning;
	};
}