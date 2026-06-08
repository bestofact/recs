static_assert(true); // -> This breaks the clangd cache, which causes a ghost clangd error otherwise.
#include "scene.h"

#include <memory>
#include <print>

namespace
{
	consteval void collect_components_recursive(
		const std::meta::info in_type,
		std::vector<std::meta::info>& out_components
	)
	{
		out_components.push_back(in_type);

		constexpr auto k_access = std::meta::access_context::unchecked();
		for (const std::meta::info member : std::meta::members_of(in_type, k_access))
		{
			if (recs::meta::has_annotation(member, recs::meta::k_component))
			{
				collect_components_recursive(member, out_components);
			}
		}
	}

	consteval std::vector<std::meta::info> collect_schema_components(const std::meta::info in_schema)
	{
		constexpr auto k_access = std::meta::access_context::unchecked();
		const std::meta::info parent_namespace = std::meta::parent_of(in_schema);
		const auto members = std::meta::members_of(parent_namespace, k_access);

		std::vector<std::meta::info> components;
		components.reserve(members.size());

		for (const std::meta::info member : members)
		{
			if (recs::meta::has_annotation(member, recs::meta::k_component))
			{
				collect_components_recursive(member, components);
			}
		}

		return components;
	}

	void seed_entities(TestScene* io_scene, test::GlobalData& out_expected)
	{
		constexpr auto k_access = std::meta::access_context::unchecked();
		constexpr recs::index k_capacity =
			recs::meta::get_annotation<typename[:recs::meta::k_schema:]>(^^test::Schema).entity_capacity;
		constexpr auto k_components = std::define_static_array(collect_schema_components(^^test::Schema));
		constexpr size_t k_entities_per_component = k_capacity / k_components.size();

		size_t component_index = 0;
		template for (constexpr std::meta::info k_component : k_components)
		{
			if constexpr (k_component == ^^test::Singleton)
			{
				continue;
			}

			// Start at 1 so entity 0 of the first split stays reserved for the Singleton (set in main).
			for (size_t local_index = 1; local_index < k_entities_per_component; ++local_index)
			{
				const recs::index entity = local_index + component_index * k_entities_per_component;
				io_scene->template set<typename[:k_component:]>(entity);

				template for (constexpr std::meta::info k_member :
					std::define_static_array(std::meta::nonstatic_data_members_of(k_component, k_access)))
				{
					using MemberType = typename[:std::meta::type_of(k_member):];
					io_scene->template get<typename[:k_component:]>(entity).[:k_member:] = static_cast<MemberType>(entity);
				}

				template for (constexpr std::meta::info k_impl_member :
					std::define_static_array(std::meta::nonstatic_data_members_of(^^test::GlobalData::Impl, k_access)))
				{
					if constexpr (std::meta::type_of(k_impl_member) == k_component)
					{
						out_expected.m_map[entity].[:k_impl_member:] =
							io_scene->template get<typename[:k_component:]>(entity);
					}
				}
			}

			++component_index;
		}
	}

	bool verify_global_data(TestScene* io_scene, const test::GlobalData& in_expected)
	{
		constexpr auto k_access = std::meta::access_context::unchecked();
		constexpr recs::index k_capacity =
			recs::meta::get_annotation<typename[:recs::meta::k_schema:]>(^^test::Schema).entity_capacity;

		const auto& actual = io_scene->get<test::GlobalData>();
		for (const recs::index entity : std::views::iota(recs::index(0), k_capacity))
		{
			if (!actual.m_map.contains(entity))
			{
				if (in_expected.m_map.contains(entity))
				{
					return false;
				}
				continue;
			}

			const auto& actual_entry = actual.m_map.at(entity);
			const auto& expected_entry = in_expected.m_map.at(entity);

			template for (constexpr std::meta::info k_member :
				std::define_static_array(std::meta::nonstatic_data_members_of(^^test::GlobalData::Impl, k_access)))
			{
				template for (constexpr std::meta::info k_sub_member :
					std::define_static_array(std::meta::nonstatic_data_members_of(std::meta::type_of(k_member), k_access)))
				{
					const auto actual_value = static_cast<recs::index>(actual_entry.[:k_member:].[:k_sub_member:]);
					const auto expected_value = static_cast<recs::index>(expected_entry.[:k_member:].[:k_sub_member:]);
					if (actual_value != expected_value)
					{
						return false;
					}
				}
			}
		}

		return true;
	}
} // namespace

int main()
{
	test::GlobalData expected;
	auto scene = std::make_unique<TestScene>();
	scene->init();

	scene->set<test::Singleton>(0);
	seed_entities(scene.get(), expected);

	scene->run();

	return verify_global_data(scene.get(), expected) ? 0 : 1;
}
