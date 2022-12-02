#include <FDTD\Grid.h>
#include <Planeverb.h>
#include <PvDefinitions.h>

#include <Context\PvContext.h>

#include <DSP\Analyzer.h>
#include <Emissions\EmissionManager.h>
#include <Util/ScopedTimer.h>
#include <omp.h>
#include <iostream>

namespace Planeverb
{
#pragma region ClientInterface
	PlaneverbOutput GetOutput(EmissionID emitter)
	{
		PlaneverbOutput out;
		std::memset(&out, 0, sizeof(out));
		auto* context = GetContext();

		// case module hasn't been created yet
		if(!context)
		{
			out.occlusion = PV_INVALID_DRY_GAIN;
			return out;
		}

		auto* analyzer = context->GetAnalyzer();
		auto* emissions = context->GetEmissionManager();
		const auto* emitterPos = emissions->GetEmitter(emitter);

		// case emitter is invalid
		if (!emitterPos)
		{
			out.occlusion = PV_INVALID_DRY_GAIN;
			return out;
		}

		auto* result = analyzer->GetResponseResult(*emitterPos);

		// case invalid emitter position
		if (!result)
		{
			out.occlusion = PV_INVALID_DRY_GAIN;
			return out;
		}

		// copy over values
		out.occlusion = (Real)result->occlusion;
        out.wetGain = (Real)result->wetGain;
		out.lowpass = (Real)result->lowpassIntensity;
		out.rt60 = (Real)result->rt60;
		out.direction = result->direction;
		out.sourceDirectivity = result->sourceDirectivity;

		return out;
	}

	std::pair<const Cell*, unsigned> GetImpulseResponse(const vec3& position)
	{
		Grid* grid = GetContext()->GetGrid();
		Real dx = grid->GetDX();
		vec2i gridPosition =
		{
			(unsigned)(position.x / dx),
			(unsigned)(position.z / dx)
		};
		return std::make_pair(grid->GetResponse(gridPosition), grid->GetResponseSize());
	}

#pragma endregion
	
	Cell* Grid::GetResponse(const vec2i& gridPosition)
	{
		unsigned index = gridPosition.x * m_gridSize.y + gridPosition.y; // INDEX((int)gridPosition.x, (int)gridPosition.y, incDim);
		return m_pulseResponse[index].data();
	}

	unsigned Grid::GetResponseSize() const
	{
		return m_responseLength;
	}
	
	// process FDTD
	void Grid::GenerateResponseCPU(const vec3 &listener)
	{
		// determine pressure and velocity update constants
		const Real Courant = PV_C * m_dt / m_dx;

		// grid constants
		const unsigned gridx = m_gridSize.x;
		const unsigned gridy = m_gridSize.y;
		const vec2i incdim = m_gridSize;
		const unsigned listenerPosX = (unsigned)((listener.x + m_gridOffset.x) / m_dx);
		const unsigned listenerPosY = (unsigned)((listener.z + m_gridOffset.y) / m_dx);
		const unsigned listenerPos = listenerPosX * gridy + listenerPosY;
		const unsigned responseLength = m_responseLength;
		unsigned loopSize = incdim.x * incdim.y;

		// thread usage
		if (m_maxThreads == 0)
			omp_set_num_threads(omp_get_max_threads());
		else
			omp_set_num_threads(m_maxThreads);

		// RESET all pressure and velocity, but not B fields (can't use memset)
		{
			Cell* resetPtr = m_grid;
            const unsigned N = loopSize;
			for (unsigned i = 0; i < N; ++i, ++resetPtr)
			{
				resetPtr->pr = 0.0;
				resetPtr->vx = 0.0;
				resetPtr->vy = 0.0;
			}
		}

		// Time-stepped FDTD simulation
		for (unsigned t = 0; t < responseLength; ++t)
		{
			// process pressure grid
			{
                const unsigned N = loopSize;
                for (unsigned i = 0; i < N; ++i)
				{
					Cell& thisCell = m_grid[i];
					int B = (int)thisCell.b;
					Real beta = (Real)B;
					//TODO: Check outside bounds access on ends?
					// [i + 1, j]
					const Cell& nextCellX = m_grid[i + gridy];	
					// [i, j + 1]
					const Cell& nextCellY = m_grid[i + 1];

					const auto divergence = ((nextCellX.vx - thisCell.vx) + (nextCellY.vy - thisCell.vy));
					thisCell.pr = beta * (thisCell.pr - Courant * divergence);
				}
			}

			// process x component of particle velocity
			{
				// eq to for(1 to sizex) for(0 to sizey)
				for (unsigned i = gridy ; i < loopSize; ++i)
				{
					// [i - 1, j]
					auto in = (i - gridy);
					const Cell& prevCell = m_grid[in];
					Real beta_n = (Real)prevCell.b;
					Real Rn = m_boundaries[in].absorption; 
					Real Yn = (1.0 - Rn) / (1.0 + Rn);

					// [i, j]
					Cell& thisCell = m_grid[i];											
					int B = (int)thisCell.b;
					Real beta = (Real)B;
					Real R = m_boundaries[i].absorption;
					Real Y = (1.0 - R) / (1.0 + R);

					const Real gradient_x = (thisCell.pr - prevCell.pr);
					const Real airCellUpdate = thisCell.vx - Courant * gradient_x;

					const Real Y_boundary = beta * Yn + beta_n * Y;
					const Real wallCellUpdate = Y_boundary * (prevCell.pr * beta_n + thisCell.pr * beta);

					thisCell.vx = beta*beta_n * airCellUpdate + (beta_n - beta) * wallCellUpdate;
				}
			}

			// process y component of particle velocity
			{
				// eq to for(0 to sizex) for(1 to sizey)
				for (unsigned i = 1; i < loopSize; ++i)
				{
					// [i, j - 1]
					const auto in = i - 1;
					const Cell& prevCell = m_grid[in];
					Real beta_n = (Real)prevCell.b;
					Real Rn = m_boundaries[in].absorption;
					Real Yn = (1.0 - Rn) / (1.0 + Rn);

					// [i, j]
					Cell& thisCell = m_grid[i];											
					int B = thisCell.b;
					Real beta = (Real)B;
					Real R = m_boundaries[i].absorption;
					Real Y = (1.0 - R) / (1.0 + R);
	
					const Real gradient_y = (thisCell.pr - prevCell.pr);
					const Real airCellUpdate = thisCell.vy - Courant * gradient_y;

					const Real Y_boundary = beta * Yn + beta_n * Y;
					const Real wallCellUpdate = Y_boundary * (prevCell.pr * beta_n + thisCell.pr * beta);

					thisCell.vy = beta * beta_n * airCellUpdate + (beta_n - beta) * wallCellUpdate;
				}
			}

			// process absorption top/bottom
			{
				for (int i = 0; i < gridy ; ++i)
				{
					unsigned index1 = i;
					unsigned index2 = gridx * gridy  + i;

					m_grid[index1].vx = -m_grid[index1].pr;
					m_grid[index2].vx = m_grid[index2 - gridy].pr;
				}
			}

			// process absorption left/right
			{
				for (unsigned i = 0; i < gridx ; ++i)
				{
					unsigned index1 = i * gridy ;
					unsigned index2 = i * gridy + gridy-1;

					m_grid[index1].vy = -m_grid[index1].pr;
					m_grid[index2].vy = m_grid[index2 - 1].pr;
				}
			}

			// add results to the response cube
			{
				for (unsigned i = 0; i < loopSize; ++i)
				{
					m_pulseResponse[i][t] = m_grid[i];
				}
			}

			// add pulse to listener position pressure field
			m_grid[listenerPos].pr += m_pulse[t];
		}
	}

	void Grid::GenerateResponseGPU(const vec3& listener)
	{
		// not currently supported
		throw pv_InvalidConfig;
	}

	void Grid::GenerateResponse(const vec3& listener)
	{
		if (m_executionType == PlaneverbExecutionType::pv_CPU)
		{
			GenerateResponseCPU(listener);
		}
		else
		{
			GenerateResponseGPU(listener);
		}
	}
} // namespace Planeverb
