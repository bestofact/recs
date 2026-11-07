#pragma once

#include "recs/private/root.h"
#include "recs/private/primitive.h"
#include "recs/private/primitive_registry.h"
#include "recs/private/manifest.h"
#include "recs/private/manifest_registry.h"
#include "recs/private/schedule.h"

#include <meta>
 
namespace recs::_private
{
	struct registry_t
	{
	public:
		consteval registry_t() = default;
		consteval explicit registry_t(const std::meta::info in_namespace);

	public:
		root_t m_root;
		primitive_registry_t m_primitives;
		manifest_registry_t m_manifests;
		schedule_t m_graph;
	};

	consteval registry_t::registry_t(const std::meta::info in_namespace)
		: m_root(in_namespace)
		, m_primitives(m_root.m_items)
		, m_manifests(m_root.m_items)
	{
	}
}

