// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Anıl Can Turan
#pragma once

#include "recs/index.h"
#include "recs/private/invalid_info.h"

#include <meta>

namespace recs
{
	struct schema final
	{
		const recs::entity_t entity_capacity;
		const std::meta::info group_enum = recs::_private::k_invalid_info;
		const std::meta::info default_group = recs::_private::k_invalid_info;
	};
} // namespace recs
