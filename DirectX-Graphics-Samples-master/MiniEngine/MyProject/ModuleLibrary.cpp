#include "pch.h"
#include "ModuleLibrary.h"
#include "ModuleContext.h"

#include "AttributeType.h"


ModuleDesc SpawnRate(float rate)
{
	ModuleDesc m;

	m.name = "Spawn Rate";
	m.stage = Stage::EmitterUpdate;
	m.reads = 
	{ 
		{"Engine.DeltaTime", &Types::Float},
		{"Emitter.SpawnCount", &Types::UInt32 },
		{"Emitter.SpawnRemainder", &Types::Float}	
	};
	m.writes =
	{
		{"Emitter.SpawnCount", &Types::UInt32 },
		{"Emitter.SpawnRemainder", &Types::Float}
	};

	m.execute = [rate](ModuleContext& ctx) 
		{
			float& remainder = ctx.Emitter<float>("Emitter.SpawnRemainder");
			uint32_t& count = ctx.Emitter<uint32_t>("Emitter.SpawnCount");

			const float want = rate * ctx.DeltaTime() + remainder;
			const uint32_t n = static_cast<uint32_t>(want);

			remainder = want - static_cast<float>(n);
			count += n;
		};

	return m;
}

ModuleDesc InitializeParticle(float size, float lifeTime, float4 color)
{
	ModuleDesc m;

	m.name = "Initialize Particle";
	m.stage = Stage::ParticleSpawn;
	m.reads =
	{
		{"Engine.Owner.Position", &Types::Float3}
	};
	m.writes =
	{
		{"Particles.Position", &Types::Float3},
		{"Particles.Color", &Types::Float4},
		{"Particles.Size", &Types::Float}
	};


	m.execute = [size, lifeTime, color](ModuleContext& ctx)
		{
			const float3 origin = ctx.OwnerPosition();

			auto pos = ctx.Particles<float3>("Particles.Position");
			auto col = ctx.Particles<float4>("Particles.Color");
			auto sizes = ctx.Particles<float>("Particles.Size");

			for (uint32_t i = ctx.Begin(); i < ctx.End(); ++i)
			{
				pos[i] = origin;
				col[i] = color;
				sizes[i] = size;
			}
		};

	return m;

}

ModuleDesc ShapeLocation(float radius, bool surfaceOnly)
{
	ModuleDesc m;
	/*m.name = "Shape Location";
	m.stage = Stage::ParticleSpawn;
	m.reads =
	{
		{"Particles.Position", &Types::Float3}
	};
	m.writes = 
	{
		{"Particles.Position", &Types::Float3}
	};

	m.execute = [radius, surfaceOnly](ModuleContext& ctx)
		{
			const float kTwoPi = 6.28318530f;

			auto pos = ctx.Particles<float3>("Particles.Position");
			
			for (uint32_t i = ctx.Begin(); i < ctx.End(); ++i)
			{
				const float z = ctx.Random01(i, 0) * 2.f - 1.f;
				const float phi = ctx.Random01(i, 1) * kTwoPi;
				const float r = std::sqrt(std::fmax(0.f, 1.f - z * z));


			}
		};*/

	return m;
	//m.execute = [radius, surfaceOnly](ModuleContext& ctx)
	//	{
	//		const float kTwoPi = 6.28318530718f;
	//		auto pos = ctx.Particles<float3>(AttrNames::Position);

	//		for (uint32_t i = ctx.Begin(); i < ctx.End(); ++i)
	//		{
	//			// salt 0, 1, 2 -> 서로 다른 난수 3개
	//			const float z = ctx.Random01(i, 0) * 2.f - 1.f;           // -1 ~ 1
	//			const float phi = ctx.Random01(i, 1) * kTwoPi;               // 0 ~ 2pi
	//			const float r = std::sqrt(std::fmax(0.f, 1.f - z * z));    // 이 높이에서 원의 반지름

	//			const float dist = surfaceOnly ? radius
	//				: radius * std::cbrt(ctx.Random01(i, 2));

	//			pos[i].x += r * std::cos(phi) * dist;
	//			pos[i].y += r * std::sin(phi) * dist;
	//			pos[i].z += z * dist;
	//		}
	//	};
}
