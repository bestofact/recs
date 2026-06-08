// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Anıl Can Turan
#pragma once

#include <meta>

namespace recs::meta
{
	struct InvalidInfo
	{
	};

	static constexpr std::meta::info k_invalid_info = ^^recs::meta::InvalidInfo;
} // namespace recs::meta
