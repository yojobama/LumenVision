#pragma once
// Tiny hand-encoded ONNX models for the area-C edge tests: no onnx Python package is needed. Each model is a ModelProto written field by
// field in protobuf wire format (ir_version 7, opset 13), saved to a temporary file and loaded by OnnxDetectionBackend like an upload.
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <variant>
#include <vector>

namespace edge_onnx {
namespace {

using Bytes = std::string;

inline void Varint(Bytes& out, uint64_t v)
{
	while (v >= 0x80) { out.push_back(static_cast<char>((v & 0x7f) | 0x80)); v >>= 7; }
	out.push_back(static_cast<char>(v));
}
inline void Key(Bytes& out, int field, int wire) { Varint(out, (static_cast<uint64_t>(field) << 3) | wire); }
inline void VarintField(Bytes& out, int field, uint64_t v) { Key(out, field, 0); Varint(out, v); }
inline void LenField(Bytes& out, int field, const Bytes& payload) { Key(out, field, 2); Varint(out, payload.size()); out += payload; }

constexpr int kFloat = 1, kInt64 = 7;

// a dimension is a fixed size or a symbolic (dynamic) name
using Dim = std::variant<int64_t, std::string>;

inline Bytes ValueInfo(const std::string& name, int elemType, const std::vector<Dim>& dims)
{
	Bytes shape;
	for (const Dim& d : dims) {
		Bytes dim;
		if (std::holds_alternative<int64_t>(d)) VarintField(dim, 1, static_cast<uint64_t>(std::get<int64_t>(d)));
		else LenField(dim, 2, std::get<std::string>(d));
		LenField(shape, 1, dim);
	}
	Bytes tensorType;
	VarintField(tensorType, 1, elemType);
	LenField(tensorType, 2, shape);
	Bytes type;
	LenField(type, 1, tensorType);
	Bytes vi;
	LenField(vi, 1, name);
	LenField(vi, 2, type);
	return vi;
}

inline Bytes TensorInt64(const std::string& name, const std::vector<int64_t>& values)
{
	Bytes t;
	VarintField(t, 1, values.size()); // dims: a 1-D tensor
	VarintField(t, 2, kInt64);
	LenField(t, 8, name);
	LenField(t, 9, Bytes(reinterpret_cast<const char*>(values.data()), values.size() * sizeof(int64_t)));
	return t;
}

inline Bytes TensorFloat(const std::string& name, const std::vector<int64_t>& dims, const std::vector<float>& values)
{
	Bytes t;
	for (int64_t d : dims) VarintField(t, 1, static_cast<uint64_t>(d));
	VarintField(t, 2, kFloat);
	LenField(t, 8, name);
	LenField(t, 9, Bytes(reinterpret_cast<const char*>(values.data()), values.size() * sizeof(float)));
	return t;
}

inline Bytes Node(const std::string& op, const std::vector<std::string>& inputs, const std::vector<std::string>& outputs)
{
	Bytes n;
	for (const std::string& i : inputs) LenField(n, 1, i);
	for (const std::string& o : outputs) LenField(n, 2, o);
	LenField(n, 3, op + "_" + outputs[0]);
	LenField(n, 4, op);
	return n;
}

struct Graph {
	std::vector<Bytes> nodes, initializers, inputs, outputs;
};

inline Bytes Model(const Graph& g)
{
	Bytes graph;
	for (const Bytes& n : g.nodes) LenField(graph, 1, n);
	LenField(graph, 2, "edge");
	for (const Bytes& t : g.initializers) LenField(graph, 5, t);
	for (const Bytes& i : g.inputs) LenField(graph, 11, i);
	for (const Bytes& o : g.outputs) LenField(graph, 12, o);

	Bytes opset;
	LenField(opset, 1, "");
	VarintField(opset, 2, 13);

	Bytes model;
	VarintField(model, 1, 7); // ir_version
	LenField(model, 2, "lumen-edge-tests");
	LenField(model, 7, graph);
	LenField(model, 8, opset);
	return model;
}

inline std::string WriteModel(const std::string& fileName, const Bytes& model)
{
	const std::filesystem::path path = std::filesystem::temp_directory_path() / fileName;
	std::ofstream(path, std::ios::binary).write(model.data(), static_cast<std::streamsize>(model.size()));
	return path.string();
}

} // namespace
} // namespace edge_onnx
