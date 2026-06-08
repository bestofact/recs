// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Anıl Can Turan
#pragma once

#include "recs/index.h"
#include "recs/detail/invalid_info.h"

#include <meta>

namespace recs
{
	struct schema final
	{
		const recs::index entity_capacity;
		const std::meta::info group_enum = recs::meta::k_invalid_info;
		const std::meta::info default_group = recs::meta::k_invalid_info;
	};
} // namespace recs
