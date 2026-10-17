#include "LauncherApp.hxx"

int main(int argc, char** argv) {
    const vibe::launcher::LauncherOptions options = vibe::launcher::parseLauncherOptions(argc, argv);
    vibe::launcher::LauncherApp app{{argc, argv}, options};
    return app.exec();
}
