// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Anıl Can Turan
#pragma once

#include <meta>

namespace recs::_private
{
	struct invalid_info_t 
	{
	};

	static constexpr std::meta::info k_invalid_info = ^^invalid_info_t;
} // namespace recs::meta
