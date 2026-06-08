// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Anıl Can Turan
#pragma once

#include "recs/detail/descriptor.h"
#include "recs/detail/meta.h"

#include <algorithm>
#include <array>
#include <meta>
#include <ranges>

namespace recs
{
	template<std::meta::info Info>
	class Storage final
	{
	};

	// Per-entity dense array. Skipped for empty components (handled by the
	// singleton specialisation below — one shared instance instead of N copies).
	template<std::meta::info Info>
	requires(
		recs::Descriptor<Info>::k_kind == recs::meta::k_component && !recs::Descriptor<Info>::k_metadata.m_is_empty
	)
	class Storage<Info> final
	{
	private:
		using ComponentDescriptor = recs::Descriptor<Info>;
		using SchemaDescriptor = recs::Descriptor<ComponentDescriptor::k_schema>;

	public:
		static consteval bool is_stored(const std::meta::info in_type)
		{
			return in_type == Info;
		}

	public:
		template<std::meta::info ReturnType>
		requires(is_stored(recs::meta::strip_type(ReturnType)))
		typename[:ReturnType:] get(const typename[:recs::meta::k_index:] in_entity_index)
		{
			auto& data = m_data[in_entity_index];
			if constexpr (std::meta::is_pointer_type(ReturnType))
			{
				return std::addressof(data);
			}
			return static_cast<typename[:ReturnType:]>(data);
		}

	public:
		std::array<typename[:Info:], SchemaDescriptor::k_metadata.m_entity_capacity> m_data;
	};

	// Singleton storage: one instance per scene. Used for resources and for
	// empty (tag) components, where a per-entity array would carry no payload.
	template<std::meta::info Info>
	requires(recs::Descriptor<Info>::k_kind == recs::meta::k_resource) ||
			(recs::Descriptor<Info>::k_kind == recs::meta::k_component && recs::Descriptor<Info>::k_metadata.m_is_empty)
	class Storage<Info> final
	{
	public:
		static consteval bool is_stored(const std::meta::info in_type)
		{
			return in_type == Info;
		}

		template<std::meta::info ReturnType>
		requires(is_stored(recs::meta::strip_type(ReturnType)))
		typename[:ReturnType:] get(const typename[:recs::meta::k_index:])
		{
			if constexpr (std::meta::is_pointer_type(ReturnType))
			{
				return std::addressof(m_data);
			}
			return static_cast<typename[:ReturnType:]>(m_data);
		}

	public:
		[:Info:] m_data;
	};

	// Scene-level storage: synthesises an aggregate whose members are the
	// per-component / per-resource storages, then routes get<T> to the right one.
	template<std::meta::info Info>
	requires(recs::Descriptor<Info>::k_kind == recs::meta::k_schema)
	class Storage<Info> final
	{
	private:
		using SchemaDescriptor = recs::Descriptor<Info>;
		struct Data;

		static consteval std::meta::info define_data()
		{
			constexpr std::meta::info k_data = ^^Data;
			constexpr std::meta::info k_storage_template = ^^recs::Storage;
			constexpr std::span<const std::meta::info> k_components = SchemaDescriptor::k_metadata.m_components;
			constexpr std::span<const std::meta::info> k_resources = SchemaDescriptor::k_metadata.m_resources;
			constexpr size_t k_component_count = k_components.size();
			constexpr size_t k_resource_count = k_resources.size();

			struct Entry
			{
				std::meta::info m_member;
				std::size_t m_alignment;
				std::size_t m_size;
			};

			std::vector<Entry> entries;
			entries.reserve(k_component_count + k_resource_count);

			for (const std::meta::info component : k_components)
			{
				const std::meta::info storage = std::meta::substitute(k_storage_template, {std::meta::reflect_constant(component)});
				const std::meta::data_member_options options{};
				const std::meta::info member = std::meta::data_member_spec(storage, options);
				entries.push_back({member, std::meta::alignment_of(component), std::meta::size_of(component)});
			}

			for (const std::meta::info resource : k_resources)
			{
				const std::meta::info storage = std::meta::substitute(k_storage_template, {std::meta::reflect_constant(resource)});
				const std::meta::data_member_options options{};
				const std::meta::info member = std::meta::data_member_spec(storage, options);
				entries.push_back({member, std::meta::alignment_of(resource), std::meta::size_of(resource)});
			}

			// Sort storage members by their stored types's size and alignment.
			std::ranges::sort(
				entries,
				[](const Entry& in_a, const Entry& in_b)
				{
					if (in_a.m_alignment != in_b.m_alignment)
					{
						return in_a.m_alignment > in_b.m_alignment;
					}
					return in_a.m_size > in_b.m_size;
				}
			);

			std::vector<std::meta::info> members;
			members.reserve(entries.size());
			for (const Entry& entry : entries)
			{
				members.push_back(entry.m_member);
			}

			return std::meta::define_aggregate(k_data, members);
		}

		consteval
		{
			define_data();
		}

	private:
		struct Metadata final
		{
			std::span<const std::meta::info> m_members;
		};

		static consteval Metadata make_metadata()
		{
			constexpr std::meta::info k_data = ^^Data;
			constexpr auto k_access_context = std::meta::access_context::unchecked();

			const auto members = std::meta::nonstatic_data_members_of(k_data, k_access_context);

			return Metadata{.m_members = std::define_static_array(members)};
		}

		static constexpr Metadata k_metadata = make_metadata();

	private:
		static consteval std::meta::info find_storage_member(const std::meta::info in_stored_type)
		{
			template for (constexpr std::meta::info k_member : k_metadata.m_members)
			{
				constexpr std::meta::info k_storage_type = std::meta::type_of(k_member);
				if ([:k_storage_type:] ::is_stored(in_stored_type))
				{
					return k_member;
				}
			}

			return recs::meta::k_invalid_info;
		}

	public:
		static consteval bool is_stored(const std::meta::info in_type)
		{
			const std::meta::info member = find_storage_member(in_type);
			return member != recs::meta::k_invalid_info;
		}

	private:

	public:
		template<std::meta::info ReturnType>
		requires(is_stored(recs::meta::strip_type(ReturnType)))
		typename[:ReturnType:] get(const typename[:recs::meta::k_index:] in_entity_index)
		{
			constexpr std::meta::info k_component_type = recs::meta::strip_type(ReturnType);
			constexpr std::meta::info k_storage_member = find_storage_member(k_component_type);

			return m_data.[:k_storage_member:].template get<ReturnType>(in_entity_index);
		}

		template<std::meta::info ReturnType>
		requires(std::meta::is_integral_type(ReturnType))
		typename[:recs::meta::k_index:] get(const typename[:recs::meta::k_index:] in_entity_index) const
		{
			return in_entity_index;
		}

	private:
		Data m_data;
	};
} // namespace recs
