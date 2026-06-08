// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Anıl Can Turan
#pragma once

#include "recs/detail/invalid_info.h"

#include <meta>

namespace recs
{
	struct system final
	{
		std::meta::info group = recs::meta::k_invalid_info;
	};
} // namespace recs
