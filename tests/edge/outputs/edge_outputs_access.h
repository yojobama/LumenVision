#pragma once
// Edge-case hunt, area E (outputs): reaches a sink's private Process()/encoder setup from a test without changing production code.
// Explicit template instantiation is exempt from access checking ([temp.explicit]/12), so Rob<Tag, &Class::PrivateMember> may name a
// private member; the friend get(Tag) it defines hands the pointer-to-member back. Each Tag is a distinct type, so every TU may use its own.
template <typename Tag, typename Tag::type Member>
struct EdgeOutputsRob {
	friend typename Tag::type get(Tag) { return Member; }
};
