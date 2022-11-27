#pragma once
#include <array>

#include "IUnityInterface.h"
#include "Planeverb.h"
#include "Context/PvContext.h"


#include "FDTD/Grid.h"
#include "FDTD/FreeGrid.h"
#include <DSP\Analyzer.h>
#include <unordered_map>

#define PVU_CC UNITY_INTERFACE_API
#define PVU_EXPORT UNITY_INTERFACE_EXPORT

extern "C"
{
#pragma region UnityPluginInterface

	void PVU_EXPORT PVU_CC UnityPluginLoad(IUnityInterfaces* unityInterfaces)
	{
		(void)unityInterfaces;
	}

	void PVU_EXPORT PVU_CC UnityPluginUnload()
	{

	}

#pragma endregion

#pragma region Export Functions
	PVU_EXPORT void PVU_CC
	PlaneverbInit(float gridSizeX, float gridSizeY,
		int gridResolution, int gridBoundaryType, char* tempFileDir, 
		int maxThreadUsage, int threadExecutionType)
	{
		Planeverb::PlaneverbConfig config;
		config.gridSizeInMeters.x = gridSizeX;
		config.gridSizeInMeters.y = gridSizeY;
		config.gridResolution = gridResolution;
		config.gridBoundaryType = (Planeverb::PlaneverbBoundaryType)gridBoundaryType;
		config.tempFileDirectory = tempFileDir;
		config.maxThreadUsage = maxThreadUsage;
		config.threadExecutionType = (Planeverb::PlaneverbExecutionType)threadExecutionType;

		Planeverb::Init(&config);
	}

	PVU_EXPORT void PVU_CC
	PlaneverbExit()
	{
		Planeverb::Exit();
	}

	PVU_EXPORT int PVU_CC
	PlaneverbEmit(float x, float y, float z)
	{
		return (int)Planeverb::Emit(Planeverb::vec3(x, y, z));
	}

	PVU_EXPORT void PVU_CC
	PlaneverbUpdateEmission(int id, float x, float y, float z)
	{
		Planeverb::UpdateEmission((Planeverb::EmissionID)id, Planeverb::vec3(x, y, z));
	}

	PVU_EXPORT void PVU_CC
	PlaneverbEndEmission(int id)
	{
		Planeverb::EndEmission((Planeverb::EmissionID)id);
	}

	struct PlaneverbOutput
	{
		float occlusion;
		float wetGain;
		float rt60;
		float lowpass;
		float directionX;
		float directionY;
		float sourceDirectionX;
		float sourceDirectionY;
	};

	PVU_EXPORT PlaneverbOutput PVU_CC
	PlaneverbGetOutput(int emissionID)
	{
		PlaneverbOutput output;
		auto poutput = Planeverb::GetOutput((Planeverb::EmissionID)emissionID);
		output.occlusion = poutput.occlusion;
		output.wetGain = poutput.wetGain;
		output.rt60 = poutput.rt60;
		output.lowpass = poutput.lowpass;
		output.directionX = poutput.direction.x;
		output.directionY = poutput.direction.y;
		output.sourceDirectionX = poutput.sourceDirectivity.x;
		output.sourceDirectionY = poutput.sourceDirectivity.y;
		return output;
	}

	PVU_EXPORT int PVU_CC
	PlaneverbAddGeometry(float posX, float posY,
		float width, float height, 
		float absorption)
	{
		Planeverb::AABB aabb;
		aabb.position.x = posX;
		aabb.position.y = posY;
		aabb.width = width;
		aabb.height = height;
		aabb.absorption = absorption;

		return (int)Planeverb::AddGeometry(&aabb);
	}

	PVU_EXPORT void PVU_CC
	PlaneverbUpdateGeometry(int id,
		float posX, float posY,
		float width, float height,
		float absorption)
	{
		Planeverb::AABB aabb;
		aabb.position.x = posX;
		aabb.position.y = posY;
		aabb.width = width;
		aabb.height = height;
		aabb.absorption = absorption;
		
		Planeverb::UpdateGeometry((Planeverb::PlaneObjectID)id, &aabb);
	}

	PVU_EXPORT void PVU_CC
	PlaneverbRemoveGeometry(int id)
	{
		Planeverb::RemoveGeometry((Planeverb::PlaneObjectID)id);
	}

	PVU_EXPORT void PVU_CC
	PlaneverbSetListenerPosition(float x, float y, float z)
	{
		Planeverb::SetListenerPosition(Planeverb::vec3(x, y, z));
	}
#pragma endregion

#pragma region FDTD Export

	extern "C++" {
		static std::vector<Planeverb::Grid*> s_userGrids;
		static std::vector<std::vector<char>> s_userMem;
	}

	PVU_EXPORT int PVU_CC
	PlaneverbGetResponsePressure(int gridId, float x, float z, float out[]) {
		auto*      context = Planeverb::GetContext();
		const auto grid = [&]() {
			if(gridId == -1) {
				return context->GetGrid();
			} else {
				return s_userGrids[gridId];
			}
		}();

		const auto offset  = grid->GetGridOffset();
		const auto gridPos = Planeverb::vec2{ (x + offset.x) / grid->GetDX(), (z + offset.y) / grid->GetDX() };

		const auto gridSize = grid->GetGridSize();
		if (gridPos.x > gridSize.x || gridPos.y > gridSize.y)
			return 0;

		const int n = grid->GetResponseSize();
		const auto data = grid->GetResponse(gridPos);
		for (int i = 0; i < n; ++i) {
			out[i] = data[i].pr;
		}
		return n;
	}



	PVU_EXPORT int PVU_CC
    PlaneverbCreateGrid(float sizeX, float sizeY, int gridResolution) {
		Planeverb::PlaneverbConfig config { };
		config.gridSizeInMeters = Planeverb::vec2{ sizeX, sizeY };
		config.gridResolution = gridResolution;
		config.tempFileDirectory = ".";
		const auto memSize = Planeverb::Grid::GetMemoryRequirement(&config);
		auto userMem = std::vector<char>(memSize);

		const auto pGrid = new Planeverb::Grid{ &config, userMem.data() };

		int id;
		for (id = 0; id < s_userGrids.size() && s_userGrids[id]; ++id);
		if (id == s_userGrids.size()) {
			s_userGrids.push_back(pGrid);
			s_userMem.emplace_back(std::move(userMem));
		} else {
			s_userGrids[id] = pGrid;
			s_userMem[id] = std::move(userMem);
		}

		return id;
	}

	PVU_EXPORT void PVU_CC
	PlaneverbDestroyGrid(int id) {
		if(id >= 0 && id < s_userGrids.size() && s_userGrids[id]) {
			delete s_userGrids[id];
			s_userGrids[id] = nullptr;
			s_userMem[id].clear();
		}
	}

	PVU_EXPORT int PVU_CC
	PlaneverbGetGridResponseLength(int id) {
		if (id >= 0 && id < s_userGrids.size() && s_userGrids[id]) {
			return s_userGrids[id]->GetResponseSize();
		} else {
			return 0;
		}
	}
	PVU_EXPORT void PVU_CC
	MDArrayTest(int arr[], int x, int y, int z) {
		for(int i=0; i<x; ++i) {
			for(int j=0; j<y; ++j) {
				for(int k=0; k<z; ++k) {
					arr[i * y * z + j * z + k] = (i+1)*(j+1)*(k+1);
				}
			}
		}
	}
	PVU_EXPORT void PVU_CC
	CopyTest(int dst[], int src[], int n) {
		std::memcpy(dst, src, n * sizeof(*dst));
	}

	PVU_EXPORT void PVU_CC
	PlaneverbGenerateGridResponse(int gridId, float listenerX, float listenerZ) {
		if (gridId >= 0 && gridId < s_userGrids.size() && s_userGrids[gridId]) {
			auto const& grid = s_userGrids[gridId];
			grid->GenerateResponse(Planeverb::vec3{ listenerX, 0, listenerZ });
		}
	}

	PVU_EXPORT void PVU_CC
	PlaneverbGetGridResponse(int gridId, float listenerX, float listenerZ, Planeverb::Cell out[]) {
		if (gridId >= 0 && gridId < s_userGrids.size() && s_userGrids[gridId]) {

			auto const& grid = s_userGrids[gridId];
			//grid->GenerateResponseCPU(Planeverb::vec3{ listenerX, 0, listenerZ });
			PlaneverbGenerateGridResponse(gridId, listenerX, listenerZ);

			const Planeverb::vec2 dim = grid->GetGridSize();

			const int xSize = static_cast<int>(dim.x + 1);
			const int ySize = static_cast<int>(dim.y + 1);
			const int zSize = static_cast<int>(grid->GetResponseSize());

			for(int x = 0; x < xSize; ++x) {
				for(int y = 0; y < ySize; ++y) {
					const auto data = grid->GetResponse(Planeverb::vec2{ static_cast<float>(x),static_cast<float>(y) });
					for (int k = 0; k < zSize; ++k) {
						out[x * (ySize * zSize) + y * zSize + k] = data[k];
					}
				}
			}
		}
	}

	PVU_EXPORT void PVU_CC
    PlaneverbAddAABB(int gridId, Planeverb::AABB aabb) {
		if (gridId >= 0 && gridId < s_userGrids.size() && s_userGrids[gridId]) {
			auto const& grid = s_userGrids[gridId];
			grid->AddAABB(&aabb);
		}
	}

	PVU_EXPORT void PVU_CC
	PlaneverbUpdateAABB(int gridId, Planeverb::AABB oldVal, Planeverb::AABB newVal) {
		if (gridId >= 0 && gridId < s_userGrids.size() && s_userGrids[gridId]) {
			auto const& grid = s_userGrids[gridId];
			grid->UpdateAABB(&oldVal, &newVal);
		}
	}

	PVU_EXPORT void PVU_CC
	PlaneverbRemoveAABB(int gridId, Planeverb::AABB aabb) {
		if (gridId >= 0 && gridId < s_userGrids.size() && s_userGrids[gridId]) {
			auto const& grid = s_userGrids[gridId];
			grid->RemoveAABB(&aabb);
		}
	}
#pragma endregion

#pragma region Analyzer Export

	extern "C++" {
		static std::vector<Planeverb::FreeGrid*> s_userFreeGrids;
		static std::vector<std::vector<char>> s_userFreeMem;
		static std::vector<Planeverb::Analyzer*> s_userAnalyzers;
		static std::vector<std::vector<char>> s_userAnaMem;
	}

	PVU_EXPORT int PVU_CC
		PlaneverbCreateFreeGrid(float sizeX, float sizeY, int gridResolution) {
		Planeverb::PlaneverbConfig config{ };
		config.gridSizeInMeters = Planeverb::vec2{ sizeX, sizeY };
		config.gridResolution = gridResolution;
		config.tempFileDirectory = ".";
		const auto memSize =  Planeverb::FreeGrid::GetMemoryRequirement(&config);
		auto userMem = std::vector<char>(memSize);

		const auto pGrid = new Planeverb::FreeGrid{ &config, userMem.data() };

		int id;
		for (id = 0; id < s_userFreeGrids.size() && s_userFreeGrids[id]; ++id);
		if (id == s_userFreeGrids.size()) {
			s_userFreeGrids.push_back(pGrid);
			s_userFreeMem.emplace_back(std::move(userMem));
		}
		else {
			s_userFreeGrids[id] = pGrid;
			s_userFreeMem[id] = std::move(userMem);
		}

		return id;
	}

	PVU_EXPORT void PVU_CC
		PlaneverbDestroyFreeGrid(int id) {
		if (id >= 0 && id < s_userFreeGrids.size() && s_userFreeGrids[id]) {
			delete s_userFreeGrids[id];
			s_userFreeGrids[id] = nullptr;
			s_userFreeMem[id].clear();
		}
	}

	PVU_EXPORT int PVU_CC
		PlaneverbCreateAnalyzer(unsigned int in_id, float sizeX, float sizeY, int gridResolution) {
		Planeverb::PlaneverbConfig config{ };
		config.gridSizeInMeters = Planeverb::vec2{ sizeX, sizeY };
		config.gridResolution = gridResolution;
		config.tempFileDirectory = ".";
		const auto memSize = Planeverb::Analyzer::GetMemoryRequirement(&config);
		auto userMem = std::vector<char>(memSize);

		const auto m_analyzer = new Planeverb::Analyzer{s_userGrids[in_id],s_userFreeGrids[in_id], userMem.data() };

		int id;
		for (id = 0; id < s_userAnalyzers.size() && s_userAnalyzers[id]; ++id);
		if (id == s_userAnalyzers.size()) {
			s_userAnalyzers.push_back(m_analyzer);
			s_userAnaMem.emplace_back(std::move(userMem));
		}
		else {
			s_userAnalyzers[id] = m_analyzer;
			s_userAnaMem[id] = std::move(userMem);
		}

		return id;
	}

	PVU_EXPORT void PVU_CC
		PlaneverbDestroyAnalyzer(int id) {
		if (id >= 0 && id < s_userAnalyzers.size() && s_userAnalyzers[id]) {
			delete s_userAnalyzers[id];
			s_userAnalyzers[id] = nullptr;
			s_userAnaMem[id].clear();
		}
	}

	PVU_EXPORT void PVU_CC
	PlaneverbAnalyzeResponses(int gridId, float listenerX, float listenerZ) {
		if (gridId >= 0 && gridId < s_userAnalyzers.size() && s_userAnalyzers[gridId]) {
			auto const& m_analyzer = s_userAnalyzers[gridId];
			m_analyzer->AnalyzeResponses(Planeverb::vec3{ listenerX, 0, listenerZ });
		}
	}

	//Not yet, need to figure out the emissionID
	// PVU_EXPORT void PVU_CC
	//	PlaneverbGetAnalyzerResponses(int gridId, Planeverb::AnalyzerResult out[]) {
	//	if (gridId >= 0 && gridId < s_userAnalyzers.size() && s_userAnalyzers[gridId]) {
	//		auto const& grid = s_userGrids[gridId];
	//		auto const& m_analyzer = s_userAnalyzers[gridId];
	//		//grid->GenerateResponseCPU(Planeverb::vec3{ listenerX, 0, listenerZ });

	//		const Planeverb::vec2 dim = grid->GetGridSize();

	//		const int xSize = static_cast<int>(dim.x + 1);
	//		const int ySize = static_cast<int>(dim.y + 1);
	//		const int zSize = static_cast<int>(grid->GetResponseSize());

	//		for (int x = 0; x < xSize; ++x) {
	//			for (int y = 0; y < ySize; ++y) {
	//				const auto data = grid->GetResponse(Planeverb::vec2{ static_cast<float>(x),static_cast<float>(y) });
	//				for (int k = 0; k < zSize; ++k) {
	//					out[x * (ySize * zSize) + y * zSize + k] = data[k];
	//				}
	//			}
	//		}
	//	}
	//}
#pragma endregion

}
