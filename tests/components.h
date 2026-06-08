#pragma once

#include "recs/component.h"

namespace test
{
	struct
	[[= recs::component{}]]
	Singleton
	{
	};

	struct
	[[= recs::component{}]]
	Tag
	{
	};

	struct
	[[= recs::component{}]]
	Data
	{
		float m_value = 0.0f;
	};

	struct
	[[= recs::component{.transient = true}]]
	TransientTag
	{
	};

	struct
	[[= recs::component{.transient = true}]]
	TransientData	
	{
		float m_value = 0.0f;
	};

	struct
	[[= recs::component{}]]
	HierarchicalTag
	{
		struct
		[[= recs::component{}]]
		ChildTag
		{
		};

		struct
		[[= recs::component{.transient = true}]]
		TransientChildTag
		{
			struct
			[[= recs::component{}]]
			ChildTag
			{
			};

			struct
			[[= recs::component{.transient = true}]]
			TransientChildTag2
			{
			};

			struct
			[[= recs::component{}]]
			ChildData
			{
				float m_value = 0.0f;
			};

			struct
			[[= recs::component{.transient = true}]]
			TransientChildData
			{
				float m_value = 0.0f;
			};
		};

		struct
		[[= recs::component{}]]
		ChildData
		{
			float m_value = 0.0f;

			struct
			[[= recs::component{}]]
			ChildTag
			{
			};

			struct
			[[= recs::component{.transient = true}]]
			TransientChildTag 
			{
			};

			struct
			[[= recs::component{}]]
			ChildData2
			{
				float m_value = 0.0f;
			};

			struct
			[[= recs::component{.transient = true}]]
			TransientChildData
			{
				float m_value = 0.0f;
			};
		};

		struct
		[[= recs::component{.transient = true}]]
		TransientChildData
		{
			float m_value = 0.0f;

			struct
			[[= recs::component{}]]
			ChildTag
			{
			};

			struct
			[[= recs::component{.transient = true}]]
			TransientChildTag 
			{
			};

			struct
			[[= recs::component{}]]
			ChildData
			{
				float m_value = 0.0f;
			};

			struct
			[[= recs::component{.transient = true}]]
			TransientChildData2
			{
				float m_value = 0.0f;
			};
		};
	};
}
