// SPDX-License-Identifier: GPL-3.0-or-later
#define SDL_MAIN_HANDLED
#include <ondra/ui.hpp>
#include <ondra/files.hpp>
#include <SDL.h>
#include <imgui.h>
#include <cstdio>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
struct Sdl {
    SDL_Window* window=NULL;
    SDL_Renderer* renderer=NULL;
    Sdl() {
        SDL_SetMainReady(); check(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER)==0,SDL_GetError());
        window=SDL_CreateWindow("Ondra UI tests",0,0,1100,800,SDL_WINDOW_HIDDEN);
        check(window!=NULL,SDL_GetError()); renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE); check(renderer!=NULL,SDL_GetError());
    }
    ~Sdl() { SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit(); }
};
struct File {
    std::string path;
    explicit File(const std::string& name):path(name) { check(ondra::canonical(path).empty(),"Test file already exists; refusing to overwrite it"); }
    ~File() {
#ifdef _WIN32
        _wremove(ondra::utf16(path).c_str());
#else
        std::remove(path.c_str());
#endif
    }
};
}
int main(int argc,char** argv) {
    try {
        Sdl sdl; ondra::Machine machine; machine.selectRom("plus");
        File config("ui-config-"+std::to_string(SDL_GetTicks())+".ini");
        const std::string saved="rom=plus\nscale=3\nsound=0\nmelodik=1\nsd=/does-not-exist\n";
        ondra::writeFile(config.path,std::vector<std::uint8_t>(saved.begin(),saved.end()));
        ondra::Options restored; restored.config=config.path; ondra::Desktop::restorePreferences(restored);
        check(restored.rom=="plus" && restored.scale==3 && restored.mute && restored.melodik && restored.sd.empty(),"Preferences did not restore or invalid SD path was accepted");
        restored.rom="tesla"; restored.scale=2; restored.explicitOptions={"--rom","--scale"}; ondra::Desktop::restorePreferences(restored);
        check(restored.rom=="tesla" && restored.scale==2,"Saved preferences overrode explicit command line options");
        restored.noConfig=true; restored.rom="basic"; restored.explicitOptions.clear(); ondra::Desktop::restorePreferences(restored);
        check(restored.rom=="basic","--no-config still loaded saved preferences");
        const std::string printerSettings="printerDevice=bt100\nmelodik=1\n";
        ondra::writeFile(config.path,std::vector<std::uint8_t>(printerSettings.begin(),printerSettings.end()));
        ondra::Options restoredPrinter; restoredPrinter.config=config.path; ondra::Desktop::restorePreferences(restoredPrinter);
        check(restoredPrinter.bt100 && !restoredPrinter.bt100Power && !restoredPrinter.melodik,"Restored BT100 must start off and exclude Melodik");
        ondra::Options overridePrinter; overridePrinter.config=config.path; overridePrinter.melodik=true; overridePrinter.explicitOptions.insert("--melodik");
        ondra::Desktop::restorePreferences(overridePrinter);
        check(overridePrinter.melodik && !overridePrinter.bt100,"Saved BT100 overrode explicit Melodik selection");
        for (unsigned n=0;n<150;++n) machine.runFrame();
        ondra::Options options; options.noConfig=true; options.mute=true;
        ondra::Desktop ui(sdl.window,sdl.renderer,machine,options);
        const auto window=SDL_GetWindowID(sdl.window);
        auto frame=[&]() { SDL_PumpEvents(); ui.draw(); ui.render(); };
        auto key=[&](SDL_Scancode code,SDL_Keycode symbol,bool down,Uint16 mod) {
            SDL_Event e={}; e.type=down?SDL_KEYDOWN:SDL_KEYUP; e.key.windowID=window;
            e.key.keysym.scancode=code; e.key.keysym.sym=symbol; e.key.keysym.mod=mod; ui.event(e); frame();
        };
        auto click=[&](const char* name) {
            frame();
            float x,y,w,h; check(ui.widgetBounds(name,x,y,w,h),(std::string("Widget missing: ")+name).c_str());
            SDL_Event e={}; e.type=SDL_MOUSEMOTION; e.motion.windowID=window; e.motion.x=static_cast<int>(x+w/2); e.motion.y=static_cast<int>(y+h/2); ui.event(e); frame();
            e.type=SDL_MOUSEBUTTONDOWN; e.button.windowID=window; e.button.button=SDL_BUTTON_LEFT; e.button.x=static_cast<int>(x+w/2); e.button.y=static_cast<int>(y+h/2); ui.event(e); frame();
            e.type=SDL_MOUSEBUTTONUP; ui.event(e); frame();
        };
        auto input=[&](const char* widget,const std::string& value) {
            click(widget); key(SDL_SCANCODE_A,SDLK_a,true,KMOD_CTRL); key(SDL_SCANCODE_A,SDLK_a,false,KMOD_NONE);
            SDL_Event e={}; e.type=SDL_TEXTINPUT; e.text.windowID=window;
            check(value.size()<sizeof(e.text.text),"Test input too long"); std::snprintf(e.text.text,sizeof(e.text.text),"%s",value.c_str()); ui.event(e); frame();
        };
        frame(); frame();
        click("Pause"); check(ui.paused(),"Pause toolbar button did not pause");
        click("Pause"); check(!ui.paused(),"Pause toolbar button did not resume");
        click("Settings"); check(ui.paused() && ui.capturesKeyboard(),"Settings did not pause and capture input");
        click("Settings.Cancel"); check(!ui.paused(),"Settings cancel did not restore run state");
        click("Debugger"); check(ui.paused() && ui.capturesKeyboard(),"Debugger did not pause");
        machine.output(3,2); machine.ram().fill(0); machine.jump(0x4000); Z80_SP(machine.cpu())=0xd000;
        machine.ram()[0x4000]=0x3e; machine.ram()[0x4001]=1; machine.ram()[0x4002]=0xcd;
        machine.ram()[0x4003]=0; machine.ram()[0x4004]=0x41; machine.ram()[0x4100]=0x3c; machine.ram()[0x4101]=0xc9;
        click("Debug.Step"); check(Z80_PC(machine.cpu())==0x4002 && Z80_A(machine.cpu())==1,"Debugger instruction step failed");
        click("Debug.Over"); check(Z80_PC(machine.cpu())==0x4005 && Z80_A(machine.cpu())==2,"Debugger step over CALL failed");
        input("Debug.HL","ABCD"); check(Z80_HL(machine.cpu())!=0xabcd,"Register changed before Enter");
        key(SDL_SCANCODE_RETURN,SDLK_RETURN,true,KMOD_NONE); key(SDL_SCANCODE_RETURN,SDLK_RETURN,false,KMOD_NONE);
        check(Z80_HL(machine.cpu())==0xabcd,"Register edit did not commit");
        input("Debug.BreakAddress0","4200");
        key(SDL_SCANCODE_RETURN,SDLK_RETURN,true,KMOD_NONE); key(SDL_SCANCODE_RETURN,SDLK_RETURN,false,KMOD_NONE);
        click("Debug.BreakEnabled0"); check(machine.breakpoints().count(0x4200)==1,"Breakpoint was not enabled");
        input("Debug.BreakAddress0","4300"); check(machine.breakpoints().count(0x4200)==1,"Breakpoint changed before Enter");
        key(SDL_SCANCODE_RETURN,SDLK_RETURN,true,KMOD_NONE); key(SDL_SCANCODE_RETURN,SDLK_RETURN,false,KMOD_NONE);
        check(!machine.breakpoints().count(0x4200) && machine.breakpoints().count(0x4300),"Editing breakpoint left a stale address");
        click("Debug.BreakEnabled0"); check(!machine.breakpoints().count(0x4300),"Disabling breakpoint failed");
        input("Debug.Memory16384","3C"); check(machine.ram()[0x4000]==0x3e,"Memory changed before Enter");
        key(SDL_SCANCODE_RETURN,SDLK_RETURN,true,KMOD_NONE); key(SDL_SCANCODE_RETURN,SDLK_RETURN,false,KMOD_NONE);
        check(machine.ram()[0x4000]==0x3c,"Memory editor did not commit");
        if (argc>1) { ui.draw(); ui.render(argv[1]); }
        // File picker must preserve UTF-8 names and the paused state, and must
        // load/save through real UI events rather than bypassing the dialog.
        const std::string name="ui-"+std::to_string(SDL_GetTicks()%100000)+"-\xc5\xbe.osn";
        File snapshot(name); machine.ram()[0x5000]=0xa5;
        click("Save snapshot"); input("File.Name",name); click("File.Accept");
        check(!ondra::canonical(name).empty(),"Snapshot save dialog did not create a file");
        check(ui.paused(),"Save dialog lost paused state"); machine.ram()[0x5000]=0; machine.jump(0x8000);
        click("Open snapshot"); input("File.Name",name); click("File.Accept");
        check(machine.ram()[0x5000]==0xa5 && Z80_PC(machine.cpu())==0x4005,"Snapshot load dialog failed");
        const auto before=ondra::readFile(name); machine.ram()[0x5000]=0x5a;
        click("Save snapshot"); input("File.Name",name); click("File.Accept");
        check(ondra::readFile(name)==before,"Existing snapshot was overwritten without confirmation");
        click("File.Keep"); click("File.Cancel");
        check(ondra::readFile(name)==before,"Cancelling overwrite changed the snapshot");
        click("Save snapshot"); input("File.Name",name); click("File.Accept"); click("File.Replace");
        check(ondra::readFile(name)!=before,"Confirmed replacement did not save the snapshot");
        File binary("ui-memory-"+std::to_string(SDL_GetTicks())+".bin");
        click("Save memory"); input("File.Name",binary.path); click("File.Accept");
        machine.ram()[0x4000]=0;
        click("Load memory"); input("File.Name",binary.path); click("File.Accept");
        check(machine.ram()[0x4000]==0x3c,"Memory BIN dialog did not restore the saved block");
        File invalid("ui-invalid-"+std::to_string(SDL_GetTicks())+".osn"); ondra::writeFile(invalid.path,{'b','a','d'});
        click("Open snapshot"); input("File.Name",invalid.path); click("File.Accept");
        check(machine.ram()[0x5000]==0x5a,"Invalid snapshot changed RAM");
        click("File.Cancel"); frame();
        float x,y,w,h; check(!ui.widgetBounds("File.Accept",x,y,w,h),"Cancel left the file dialog open");
        File printerFile("ui-prn-\xc5\xbe-"+std::to_string(SDL_GetTicks()%100000)+".prn");
        machine.sound().setMelodik(true);
        click("Settings"); click("Settings.Printers"); click("Settings.Centronics");
        input("Settings.PrinterFile",printerFile.path); click("Settings.Apply");
        check(machine.printer().enabled() && !machine.sound().melodik() && !machine.bt100().enabled(),"Selecting Centronics did not disconnect Melodik");
        machine.output(0x0a,8); machine.output(9,'A'); machine.output(0x0a,0);
        check(ondra::readFile(printerFile.path)==std::vector<std::uint8_t>({'A'}),"Centronics UI destination did not receive printer data");
        click("Settings"); click("Settings.Printers"); click("Settings.BT100"); click("Settings.Apply");
        check(machine.bt100().enabled() && !machine.bt100().powered() && !machine.printer().enabled(),"BT100 connection/power state is wrong");
        click("Printer.Power"); check(machine.bt100().powered(),"BT100 power button failed");
        machine.output(9,255); machine.output(9,254); check(machine.bt100().dot(0,0),"BT100 did not strike paper");
        File paper("ui-paper-"+std::to_string(SDL_GetTicks())+".png");
        click("Printer.Save"); input("File.Name",paper.path); click("File.Accept");
        const auto png=ondra::readFile(paper.path);
        check(png.size()>32 && png[0]==137 && png[1]=='P' && png[2]=='N' && png[3]=='G',"BT100 PNG export failed");
        click("Printer.New"); check(!machine.bt100().dot(0,0) && machine.bt100().row()==0,"BT100 new paper did not clear output");
        click("Printer.Power"); check(!machine.bt100().powered(),"BT100 power off failed");
        key(SDL_SCANCODE_F6,SDLK_F6,true,KMOD_NONE); key(SDL_SCANCODE_F6,SDLK_F6,false,KMOD_NONE);
        check(machine.turbo(),"Turbo keyboard shortcut failed");
        SDL_Event quit={}; quit.type=SDL_QUIT; ui.event(quit); check(!ui.running(),"Quit event did not stop desktop");
        std::cout<<"PASS UI: preferences, pause/run, settings/input capture, debugger step/step-over, register/memory/breakpoint edits, UTF-8 snapshot save/load, overwrite confirmation, BIN save/load, invalid-file recovery, printers/PNG/paper/power, cancel, turbo, quit\n";
        return 0;
    } catch (const std::exception& e) { std::cerr<<"FAIL UI: "<<e.what()<<'\n'; return 1; }
}
