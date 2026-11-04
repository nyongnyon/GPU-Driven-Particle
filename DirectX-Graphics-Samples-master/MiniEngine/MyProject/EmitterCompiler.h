#pragma once
#include "ModuleDesc.h"
#include "CompiledEmitter.h"

class EmitterCompiler
{
public:
	EmitterCompiler& Add(const ModuleDesc& m) { modules_.push_back(m); return *this; }
	EmitterCompiler& Add(const RendererDesc& r) { renderers_.push_back(r); return *this; }
	EmitterCompiler& SetTrim(bool b) { trim_ = b; return *this; }

	CompiledEmitter Compile() const;

private:
	std::vector<ModuleDesc>   modules_;
	std::vector<RendererDesc> renderers_;
	bool trim_ = true;
};

