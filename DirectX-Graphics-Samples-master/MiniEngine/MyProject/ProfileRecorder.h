#pragma once
#include <cstdint>
#include <string>
#include <vector>

class ProfileRecorder
{
public:
	void SetTimers(const std::vector<std::wstring>& names);
	void Start(uint32_t warmupFrames, uint32_t recordFrames);
	void Tick(uint32_t aliveCount);

	bool		IsBusy() const { return m_State != State::Idle; }
	float		GetProgress() const;
	const char* GetStateText() const;

	bool AppendCSV(const std::string& path, const std::string& mode, const std::string& tag, uint32_t maxParticles) const;

};

