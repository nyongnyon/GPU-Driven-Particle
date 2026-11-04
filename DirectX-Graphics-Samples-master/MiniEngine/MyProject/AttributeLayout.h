#pragma once
#include "ModuleDesc.h"

#include <vector>
#include <string>

struct AttributeLayout
{
	Usage desc;
	int startComponent;		// SoA
};

struct ParticleLayout
{
	std::vector<AttributeLayout> attributes;
	int totalComponent = 0;
	int Find(const std::string& name);
};

