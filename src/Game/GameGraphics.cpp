#include "GPU_Globals.h"

/* The part of the graphics that is the game's (the pools and the declarations are in Game/GameGraphicsGlobals.h). */
namespace Graphics
{
	void initGameEffects()
	{
		dashTrail.init();
	}

	void tickGameEffects(float tickTime_)
	{
		dashTrail.tick();
		effectBalls.tick(tickTime_);
		dashSpheres.tick();
		sonicTrail.tick();
	}

	void drawGameEffects()
	{
		dashTrail.draw();
		effectBalls.draw();
		dashSpheres.draw();
		sonicTrail.draw();
	}

	void destroyGameGraphics()
	{
		dashTrail.destroy();
		effectBalls.destroy();
		dashSpheres.destroy();
		sonicTrail.destroy();
		oneArchives.clear();
		saMeshes.clear();
		relModules.clear();
	}
}
