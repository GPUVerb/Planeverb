#include <FDTD\Grid.h>
#include <PvDefinitions.h>
#include <cmath>
#include <cstring>
#include <iostream>

namespace Planeverb
{
	namespace
	{
		// Fill a given array with a precomputed Gaussian pulse
		void GaussianPulse(const PlaneverbConfig* config, Real samplingRate, Real* out, unsigned numSamples)
		{
            const Real maxFreq = Real(config->gridResolution);
            const Real pi = std::acos(-1);
			Real sigma = (Real)1.0f / (0.5 * pi * maxFreq);

			const Real delay = 2*sigma;
            const Real dt = (Real)1.0f / samplingRate;

            for (unsigned i = 0; i < numSamples; ++i)
			{
				Real t = (Real)i * dt;
				Real val = std::exp(-(t - delay) * (t - delay) / (sigma * sigma));
				*out++ = val;
			}
		}
	} // namespace <>

	Grid::Grid(const PlaneverbConfig* config, char* mem) :
		m_mem(mem),
		m_grid(nullptr),
		m_boundaries(nullptr),
		m_pulseResponse(nullptr),
		m_pulse(nullptr),
		m_dx(), m_dt(),
		m_gridSize(), m_gridDimensions(config->gridSizeInMeters), m_gridOffset(), m_responseLength(),
		m_samplingRate(),
		m_resolution(config->gridResolution),
		m_executionType(config->threadExecutionType),
		m_maxThreads(config->maxThreadUsage)
	{
		// calculate internals
		m_gridOffset = config->gridWorldOffset;

		CalculateGridParameters(config->gridResolution, m_dx, m_dt, m_samplingRate);

		m_gridSize.x = static_cast<unsigned>((1.f / m_dx) * m_gridDimensions.x + 1.0f);
		m_gridSize.y = static_cast<unsigned>((1.f / m_dx) * m_gridDimensions.y + 1.0f);

		// calculate total memory size
		// length per grid uses gridsize + 1 for extended velocity fields
		unsigned lengthPerGrid = m_gridSize.x * m_gridSize.y ;
		unsigned sizePerBoundary = sizeof(BoundaryInfo) * lengthPerGrid;
		unsigned lengthPerResponse = (unsigned)(m_samplingRate * PV_IMPULSE_RESPONSE_S); 
		unsigned size =
			lengthPerResponse * sizeof(Real) +	// memory for Gaussian pulse values
			lengthPerGrid * sizeof(Cell) +		// memory for Cell grid
			sizePerBoundary +	// memory for boundary information

			/// memory for pulse response Cell[x][y][t]
			///sizePerGrid * lengthPerResponse;
			// memory for pulse response std::vector<Cell>[x][y]
			lengthPerGrid * sizeof(std::vector<Cell>);

		// allocate memory pool, throw for operator new fails. set memory to zero
		if (!m_mem)
		{
			throw pv_NotEnoughMemory;
		}
		std::memset(m_mem, 0, size);

		// set grids and arrays offset into pool
		char* temp = m_mem;
		m_pulse = reinterpret_cast<Real*>(temp);				temp += lengthPerResponse * sizeof(Real);
		m_grid = reinterpret_cast<Cell*>(temp);					temp += lengthPerGrid * sizeof(Cell);
		m_boundaries = reinterpret_cast<BoundaryInfo*>(temp);	temp += sizePerBoundary;
		m_pulseResponse = reinterpret_cast<std::vector<Cell>*>(temp);

		vec2i incGridSize(m_gridSize.x , m_gridSize.y );
		m_responseLength = lengthPerResponse;

		// init the boundary layer
		for (unsigned i = 0; i < incGridSize.x; ++i)
			for (unsigned j = 0; j < incGridSize.y; ++j)
				m_boundaries[INDEX(i, j, incGridSize)] = BoundaryInfo{ vec2((Real)0.f, (Real)0.f), PV_ABSORPTION_FREE_SPACE };

		// init the b and by field
		unsigned numBIterations = incGridSize.x * incGridSize.y;
		for (unsigned i = 0; i < numBIterations; ++i)
		{
			unsigned row = i / incGridSize.y;
			unsigned col = i % incGridSize.y;
			if (row == m_gridSize.x || col == m_gridSize.y)
			{
				// TODO: delete this? this will never be entered
				m_grid[i].b = 0;
				m_grid[i].by = 0;
			}
			else if (col == 0)
			{
				m_grid[i].b = 1;
				m_grid[i].by = 0;
			}
			else
			{
				m_grid[i].b = 1;
				m_grid[i].by = 1;
			}

			// initialize pulseResponse
			new (&m_pulseResponse[i]) std::vector<Cell>(); // placement new to call ctor
			m_pulseResponse[i].resize(lengthPerResponse, Cell());
		}

		// precompute Gaussian pulse
		GaussianPulse(config, m_samplingRate, m_pulse, m_responseLength);
	}

	Grid::~Grid()
	{
		if (m_mem)
		{
			unsigned loopSize = m_gridSize.x * m_gridSize.y;
			// destruct each vector
			for (unsigned i = 0; i < loopSize; ++i)
			{
				m_pulseResponse[i].~vector();
			}

			// delete the pool
			//delete[] m_mem;
			//m_mem = nullptr;
		}
	}

	void Grid::AddAABB(const AABB * transform)
	{
		// define edges of the AABB
		const unsigned startY = (unsigned)((transform->position.y - transform->height / (Real)2.f + m_gridOffset.x) * ((Real)1.f / m_dx));
		const unsigned startX = (unsigned)((transform->position.x - transform->width  / (Real)2.f + m_gridOffset.y) * ((Real)1.f / m_dx));
		const unsigned endY   = (unsigned)((transform->position.y + transform->height / (Real)2.f + m_gridOffset.x) * ((Real)1.f / m_dx));
		const unsigned endX   = (unsigned)((transform->position.x + transform->width  / (Real)2.f + m_gridOffset.y) * ((Real)1.f / m_dx));
		
		const vec2i newGridSize(m_gridSize.x, m_gridSize.y);

		/*
		// top
		if (startY >= 0 && startY < m_gridSize.y)
		{
			for (int i = startX; i < endX - 1; ++i)
			{
				if (i >= 0 && i < m_gridSize.x)
				{
					int index = INDEX(i, startY, newGridSize);
					m_boundaries[index].normal = vec2(-1, 0);
					m_boundaries[index].absorption = transform->absorption;
					m_grid[index].b = 0;
					m_grid[index].by = 0;
				}
			}
		}
		// bottom
		if (endY >= 0 && endY < m_gridSize.y)
		{
			for (int i = startX; i < endX; ++i)
			{
				if (i >= 0 && i < m_gridSize.x)
				{
					int index = INDEX(i, endY - 1, newGridSize);
					m_boundaries[index].normal = vec2(1, 0);
					m_boundaries[index].absorption = transform->absorption;
					m_grid[index].b = 0;
					m_grid[index].by = 0;
				}
			}
		}

		// left
		if (startX >= 0 && startX < m_gridSize.x)
		{
			for (int i = startY; i < endY; ++i)
			{
				if (i >= 0 && i < m_gridSize.y)
				{
					int index = INDEX(startX, i, newGridSize);
					m_boundaries[index].normal = vec2(0, -1);
					m_boundaries[index].absorption = transform->absorption;
					m_grid[index].b = 0;
					m_grid[index].by = 0;
				}
			}
		}
		// right
		if (endX >= 0 && endX < m_gridSize.x)
		{
			for (int i = startY; i < endY; ++i)
			{
				if (i >= 0 && i < m_gridSize.y)
				{
					int index = INDEX(endX - 1, i, newGridSize);
					m_boundaries[index].normal = vec2(0, 1);
					m_boundaries[index].absorption = transform->absorption;
					m_grid[index].b = 0;
					m_grid[index].by = 0;
				}
			}
		}

		// inside
		for (int i = startY + 1; i < endY - 1; ++i)
		{
			if (i >= 0 && i < m_gridSize.y)
			{
				for (int j = startX + 1; j < endX - 1; ++j)
				{
					if (j >= 0 && j < m_gridSize.x)
					{
						int index = INDEX(j, i, newGridSize);
						m_boundaries[index].normal = vec2(0, 0);
						m_boundaries[index].absorption = PV_ABSORPTION_FREE_SPACE;
						m_grid[index].b = 0;
						m_grid[index].by = 0;
					}
				}
			}
		}
		*/

		for (unsigned i = startY; i < endY; ++i)
		{
			if (i >= 0 && i <= m_gridSize.y)
			{
				for (unsigned j = startX; j < endX; ++j)
				{
					if (j >= 0 && j <= m_gridSize.x)
					{
						unsigned index = INDEX(j, i, newGridSize);
						m_boundaries[index].normal = vec2(0, 0);
						m_boundaries[index].absorption = transform->absorption;

						m_grid[index].b = 0;
						m_grid[index].by = 0;
					}
				}
			}
		}
	}

	void Grid::RemoveAABB(const AABB * transform)
	{
		// define edges of the AABB
		unsigned startY = (unsigned)((transform->position.y - transform->height / (Real)2.f + m_gridOffset.y) * ((Real)1.f / m_dx));
		unsigned startX = (unsigned)((transform->position.x - transform->width  / (Real)2.f + m_gridOffset.x) * ((Real)1.f / m_dx));
		unsigned endY   = (unsigned)((transform->position.y + transform->height / (Real)2.f + m_gridOffset.y) * ((Real)1.f / m_dx));
		unsigned endX   = (unsigned)((transform->position.x + transform->width  / (Real)2.f + m_gridOffset.x) * ((Real)1.f / m_dx));

		vec2i newGridSize(m_gridSize.x, m_gridSize.y );

		// reset area of the AABB
		for (unsigned i = startY; i < endY; ++i)
		{
			if (i >= 0 && i <= m_gridSize.y)
			{
				for (unsigned j = startX; j < endX; ++j)
				{
					if (j >= 0 && j <= m_gridSize.x)
					{
						unsigned index = INDEX(j, i, newGridSize);
						m_boundaries[index].normal = vec2(0, 0);
						m_boundaries[index].absorption = PV_ABSORPTION_FREE_SPACE;
						
						m_grid[index].b = 1;
						m_grid[index].by = 1;

						
						if (i == m_gridSize.x || j == m_gridSize.y)
						{
							m_grid[index].b = 0;
							m_grid[index].by = 0;
						}
						else if (j == 0)
						{
							m_grid[index].b = 1;
							m_grid[index].by = 0;
						}
						else
						{
							m_grid[index].b = 1;
							m_grid[index].by = 1;
						}
						
					}
				}
			}
		}
	}

	void Grid::UpdateAABB(const AABB * oldTransform, const AABB * newTransform)
	{
		// remove then re-add the new AABB
		RemoveAABB(oldTransform);
		AddAABB(newTransform);
	}

	// Debug print the grid
	void Grid::PrintGrid()
	{
		unsigned gridx = m_gridSize.x;
		unsigned gridy = m_gridSize.y;
		vec2i newGridSize(gridx, gridy);

		for (unsigned i = 0; i < gridx - 1; ++i)
		{
			for (unsigned j = 0; j < gridy - 1; ++j)
			{
				unsigned index = INDEX(i, j, newGridSize);

				/* old version based off of normal
				if(m_boundaries[index].normal.x == m_boundaries[index].normal.y && m_boundaries[index].normal.x == 0)
				{
					std::cout << " .";
				}
				else
				{
					if (m_boundaries[index].normal.x != 0.f)
					{
						if (m_boundaries[index].normal.x > 0)
							std::cout << "x>";
						else
							std::cout << "<x";
					}
					else
					{
						if (m_boundaries[index].normal.y > 0)
							std::cout << "yv";
						else
							std::cout << "y^";
					}
				}*/

				const Cell& cell = m_grid[index];
				if (cell.b || cell.by)
				{
					std::cout << " .";
				}
				else
				{
					std::cout << "00";
				}

			}

			std::cout << std::endl;
		}

		std::cout << std::endl;
	}

	unsigned Grid::GetMemoryRequirement(const PlaneverbConfig * config)
	{
		// calculate internals
		vec2 m_gridOffset = config->gridWorldOffset;

		Real m_dx, m_dt;
		unsigned m_samplingRate;
		CalculateGridParameters(config->gridResolution, m_dx, m_dt, m_samplingRate);

		vec2i m_gridSize;
		m_gridSize.x = (unsigned)((1.f / m_dx) * config->gridSizeInMeters.x + 1);
		m_gridSize.y = (unsigned)((1.f / m_dx) * config->gridSizeInMeters.y + 1);

		// calculate total memory size
		// length per grid uses gridsize + 1 for extended velocity fields
		unsigned lengthPerGrid = m_gridSize.x * m_gridSize.y;
		unsigned sizePerBoundary = sizeof(BoundaryInfo) * lengthPerGrid;
		unsigned lengthPerResponse = (unsigned)(m_samplingRate * PV_IMPULSE_RESPONSE_S);
		unsigned size =
			lengthPerResponse * sizeof(Real) +	// memory for Gaussian pulse values
			lengthPerGrid * sizeof(Cell) +		// memory for Cell grid
			sizePerBoundary +	// memory for boundary information

			/// memory for pulse response Cell[x][y][t]
			///sizePerGrid * lengthPerResponse;
			// memory for pulse response std::vector<Cell>[x][y]
			lengthPerGrid * sizeof(std::vector<Cell>);

		return size;
	}

	void CalculateGridParameters(int resolution, Real & dx, Real & dt, unsigned & samplingRate)
	{
		Real minWavelength = PV_C / (Real)resolution;
		dx = minWavelength / PV_POINTS_PER_WAVELENGTH;
		dt = dx / (PV_C * Real(1.5));
		samplingRate = (unsigned)(Real(1.0) / dt);
	}
} // namespace Planeverb
