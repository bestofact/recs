#pragma once

#include "group.h"

#include "recs/schema.h"

namespace test
{
	struct 
	[[
		= recs::schema{
			.entity_capacity = 1'000'000,
			.group_enum = ^^test::Group,
			.default_group = ^^test::Group::Update
		}
	]]
	Schema 
	{
	};
}
