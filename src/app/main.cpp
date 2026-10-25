#include "app/application.h"

int main(int argumentCount, char** argumentValues) {
	SteamForge::Application application(argumentCount, argumentValues);
	return application.Run();
}
