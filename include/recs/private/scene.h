#pragma once

#include "recs/private/manifest.h"
#include "recs/private/primitive.h"
#include "recs/private/primitive_database.h"

#include <meta>

namespace recs::_private
{
	struct registry_t
	{
	public:
		consteval registry_t() = default;
		consteval explicit registry_t(const std::meta::info in_root);

	public:
		primitive_registry_t m_primitives;
	};

	consteval registry_t::registry_t(const std::meta::info in_root)
	{
		m_primitives = primitive_database_t(in_root);
		
	}
} // namespace recs::_private
