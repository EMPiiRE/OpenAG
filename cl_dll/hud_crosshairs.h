#pragma once
#include <vector>

class CHudCrosshairs : public CHudBase
{
	cvar_t* cl_cross;
	cvar_t* cl_cross_color;
	cvar_t* cl_cross_alpha;
	cvar_t* cl_cross_thickness;
	cvar_t* cl_cross_size;
	cvar_t* cl_cross_gap;
	cvar_t* cl_cross_circle_radius;
	cvar_t* cl_cross_dot_size;

	float old_circle_radius;
	std::vector<Vector2D> circle_points;

public:
	virtual int Init();
	virtual int VidInit();
	virtual int Draw(float time);
};
