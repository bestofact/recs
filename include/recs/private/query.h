#pragma once

#include "recs/private/entity.h"

#include <meta>
#include <bitset>
#include <vector>

namespace recs::_private
{
	struct query_t
	{
		consteval 
		{
			
		}

		std::bitset<Size> m_includes;
		std::bitset<Size> m_excludes;
		std::vector<entity_t> m_entities;
	};

}
