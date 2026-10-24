// SPDX-License-Identifier: GPL-3.0-or-later
#include <ondra/ui.hpp>
#include <ondra/disassembler.hpp>
#include <ondra/files.hpp>
#include <ondra/resources.hpp>
#include <SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_sdlrenderer2.h>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <SDL_syswm.h>
#endif
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <stdexcept>

namespace ondra {
namespace {
typedef std::array<char,4096> Path;
void text(Path& target,const std::string& value) { std::snprintf(target.data(),target.size(),"%s",value.c_str()); }
std::string friendly(std::string path) {
    if (path.compare(0,8,"//?/UNC/")==0) return "//"+path.substr(8);
    if (path.compare(0,4,"//?/")==0) return path.substr(4);
    return path;
}
std::string baseName(const std::string& path) { const auto p=path.find_last_of("/\\"); return p==std::string::npos?path:path.substr(p+1); }
std::string parent(const std::string& path) {
    std::string p=canonical(path+"/.."); return p.empty()?path:friendly(p);
}
std::string configPath(const Options& options) {
    if (options.noConfig) return "";
    if (!options.config.empty()) return options.config;
    char* base=SDL_GetBasePath(); const std::string path=base?std::string(base)+"ondra.ini":"ondra.ini";
    SDL_free(base); return path;
}
std::string escape(const std::string& value) {
    std::string result;
    for (char c:value) { if (c=='\\') result+="\\\\"; else if (c=='\n') result+="\\n"; else if (c=='\r') result+="\\r"; else result+=c; }
    return result;
}
std::string unescape(const std::string& value) {
    std::string result;
    for (std::size_t i=0;i<value.size();++i) {
        if (value[i]=='\\' && i+1<value.size()) { const char c=value[++i]; result+=c=='n'?'\n':c=='r'?'\r':c; }
        else result+=value[i];
    }
    return result;
}
std::map<std::string,std::string> preferences(const Options& options) {
    std::map<std::string,std::string> values; const auto path=configPath(options);
    if (path.empty() || canonical(path).empty()) return values;
    try {
        const auto data=readFile(path,128*1024); std::istringstream lines(std::string(data.begin(),data.end())); std::string line;
        while (std::getline(lines,line)) {
            if (!line.empty() && line.back()=='\r') line.pop_back();
            const auto equal=line.find('=');
            if (!line.empty() && line[0]!='#' && equal!=std::string::npos) values[line.substr(0,equal)]=unescape(line.substr(equal+1));
        }
    } catch (const std::exception&) { /* A preferences file must not prevent startup. */ }
    return values;
}
void sdl(bool ok,const char* operation) { if (!ok) throw std::runtime_error(std::string(operation)+": "+SDL_GetError()); }
ImTextureID imageId(SDL_Texture* texture) { return static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(texture)); }
void tip(const char* text) { if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s",text); }
bool hexInput(const char* label,unsigned& value,unsigned width=4) {
    char buffer[24]; std::snprintf(buffer,sizeof(buffer),width==2?"%02X":"%04X",value);
    if (!ImGui::InputText(label,buffer,sizeof(buffer),ImGuiInputTextFlags_CharsHexadecimal|ImGuiInputTextFlags_EnterReturnsTrue)) return false;
    char* end=NULL; const auto parsed=std::strtoull(buffer,&end,16);
    if (end==buffer || *end || parsed>0xffffffffull) return false;
    value=static_cast<unsigned>(parsed); return true;
}
const char* romNames[]={"Basic EXP v5","Tesla ROM","SSM / ViLi v25","SSM / ViLi v27","Ondra+ ROM","Custom ROM"};
const char* romIds[]={"basic","tesla","vili25","vili27","plus","custom"};
int romIndex(const std::string& name) { for (int i=0;i<6;++i) if (name==romIds[i]) return i; return 0; }

struct ContextScope {
    ImGuiContext* previous;
    explicit ContextScope(ImGuiContext* context):previous(ImGui::GetCurrentContext()) { ImGui::SetCurrentContext(context); }
    ~ContextScope() { ImGui::SetCurrentContext(previous); }
};
void configureContext(std::vector<std::uint8_t>& font) {
    ImGui::GetIO().IniFilename=NULL;
    ImGui::StyleColorsLight();
    auto& style=ImGui::GetStyle(); style.WindowRounding=3; style.FrameRounding=2; style.PopupRounding=3;
    style.FramePadding=ImVec2(5,4); style.ItemSpacing=ImVec2(7,6); style.WindowBorderSize=1;
    style.Colors[ImGuiCol_WindowBg]=ImVec4(0.94f,0.94f,0.94f,1);
    style.Colors[ImGuiCol_PopupBg]=ImVec4(0.94f,0.94f,0.94f,1);
    style.Colors[ImGuiCol_MenuBarBg]=ImVec4(0.93f,0.93f,0.93f,1);
    style.Colors[ImGuiCol_Button]=ImVec4(0.86f,0.87f,0.88f,1);
    style.Colors[ImGuiCol_Header]=ImVec4(0.68f,0.78f,0.9f,1);
    ImFontConfig fc; fc.FontDataOwnedByAtlas=false;
    static const ImWchar ranges[]={0x20,0x17f,0x2000,0x206f,0};
    ImGui::GetIO().Fonts->AddFontFromMemoryTTF(font.data(),static_cast<int>(font.size()),15,&fc,ranges);
}
SDL_Texture* loadIcon(SDL_Renderer* renderer,const char* name) {
    const auto data=resource(std::string(name)+".rgba"); const unsigned width=dword(data,0),height=dword(data,4);
    if (data.size()!=8+width*height*4) throw std::runtime_error("Invalid UI image");
    SDL_Surface* surface=SDL_CreateRGBSurfaceWithFormatFrom(const_cast<std::uint8_t*>(data.data()+8),width,height,32,width*4,SDL_PIXELFORMAT_RGBA32);
    sdl(surface!=NULL,"Load UI image");
    SDL_Texture* texture=SDL_CreateTextureFromSurface(renderer,surface); SDL_FreeSurface(surface);
    sdl(texture!=NULL,"Create UI image"); SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_BLEND); return texture;
}
// Each SDL renderer needs its own font atlas and textures. Backend state is
// context-local; events must be routed by SDL window ID before calling ImGui.
struct ToolWindow {
    SDL_Window* window=NULL;
    SDL_Renderer* renderer=NULL;
    ImGuiContext* context=NULL;
    std::map<std::string,SDL_Texture*> icons;
    bool platformReady=false,rendererReady=false,frameReady=false;
    ToolWindow(SDL_Window* parent,std::vector<std::uint8_t>& font,const char* title,int width,int height,int minWidth,int minHeight) {
        ContextScope restore(ImGui::GetCurrentContext());
        try {
            int x=0,y=0,w=0,h=0; SDL_GetWindowPosition(parent,&x,&y); SDL_GetWindowSize(parent,&w,&h);
            SDL_Rect display={x,y,width,height};
            SDL_GetDisplayUsableBounds(SDL_GetWindowDisplayIndex(parent),&display);
            width=std::min(width,display.w); height=std::min(height,display.h);
            x=std::max(display.x,std::min(x+(w-width)/2,display.x+display.w-width));
            y=std::max(display.y,std::min(y+(h-height)/2,display.y+display.h-height));
            window=SDL_CreateWindow(title,x,y,width,height,SDL_WINDOW_HIDDEN|SDL_WINDOW_RESIZABLE|SDL_WINDOW_ALLOW_HIGHDPI|SDL_WINDOW_UTILITY);
            sdl(window!=NULL,"Create tool window");
            SDL_SetWindowMinimumSize(window,std::min(minWidth,width),std::min(minHeight,height));
            renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED);
            if (!renderer) renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);
            sdl(renderer!=NULL,"Create tool renderer");
            // Creating a renderer can recreate the native window (e.g. to
            // add OpenGL flags), so attach the parent after that operation.
#ifdef _WIN32
            SDL_SysWMinfo childInfo={},parentInfo={}; SDL_VERSION(&childInfo.version); SDL_VERSION(&parentInfo.version);
            if (SDL_GetWindowWMInfo(window,&childInfo) && SDL_GetWindowWMInfo(parent,&parentInfo) && childInfo.subsystem==SDL_SYSWM_WINDOWS && parentInfo.subsystem==SDL_SYSWM_WINDOWS)
                SetWindowLongPtrW(childInfo.info.win.window,GWLP_HWNDPARENT,reinterpret_cast<LONG_PTR>(parentInfo.info.win.window));
#else
            // X11 implements this as WM_TRANSIENT_FOR without blocking the
            // parent. Other SDL drivers may leave this relationship unsupported.
            if (SDL_SetWindowModalFor(window,parent)!=0) SDL_ClearError();
#endif
            context=ImGui::CreateContext(); ImGui::SetCurrentContext(context); configureContext(font);
            platformReady=ImGui_ImplSDL2_InitForSDLRenderer(window,renderer); sdl(platformReady,"Tool window input");
            rendererReady=ImGui_ImplSDLRenderer2_Init(renderer); sdl(rendererReady,"Tool window renderer");
            for (const char* name:{"rundbg","step","stepover","reset"}) icons[name]=loadIcon(renderer,name);
            if (!(SDL_GetWindowFlags(parent)&SDL_WINDOW_HIDDEN)) SDL_ShowWindow(window);
            SDL_RaiseWindow(window);
        } catch (...) { release(); throw; }
    }
    ~ToolWindow() { release(); }
    void release() {
        ImGuiContext* previous=ImGui::GetCurrentContext(); ImGui::SetCurrentContext(context);
        for (auto& icon:icons) SDL_DestroyTexture(icon.second);
        icons.clear();
        if (rendererReady) ImGui_ImplSDLRenderer2_Shutdown();
        if (platformReady) ImGui_ImplSDL2_Shutdown();
        if (context) ImGui::DestroyContext(context);
        ImGui::SetCurrentContext(previous==context?NULL:previous);
        SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window);
        context=NULL; renderer=NULL; window=NULL; platformReady=rendererReady=false;
    }
    void begin() {
        ImGui::SetCurrentContext(context);
        if (SDL_GetKeyboardFocus()==window) ImGui::GetIO().ConfigFlags&=~ImGuiConfigFlags_NoMouseCursorChange;
        else ImGui::GetIO().ConfigFlags|=ImGuiConfigFlags_NoMouseCursorChange;
        ImGui_ImplSDLRenderer2_NewFrame(); ImGui_ImplSDL2_NewFrame(); ImGui::NewFrame(); frameReady=true;
    }
    void render() {
        if (!frameReady) return;
        ContextScope active(context); ImGui::Render();
        SDL_SetRenderDrawColor(renderer,240,240,240,255); SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(),renderer); SDL_RenderPresent(renderer); frameReady=false;
    }
};
}

void Desktop::restorePreferences(Options& options) {
    const auto values=preferences(options);
    auto get=[&](const char* key,const std::string& fallback) { const auto p=values.find(key); return p==values.end()?fallback:p->second; };
    const auto& args=options.explicitOptions;
    if (!args.count("--rom") && !args.count("--rom-a") && !args.count("--rom-b")) {
        const auto rom=get("rom",options.rom);
        if (rom=="custom") {
            const auto a=get("romA",""),b=get("romB","");
            if (!a.empty() && !b.empty() && !canonical(a).empty() && !canonical(b).empty()) { options.romA=a; options.romB=b; }
        } else if (romIndex(rom)<5 && std::string(romIds[romIndex(rom)])==rom) options.rom=rom;
    }
    if (!args.count("--sd")) { const auto path=get("sd",""); if (!path.empty() && isDirectory(path)) options.sd=path; }
    options.sdRtc=get("sdRtc",get("SDRTC",""));
    if (!args.count("--turbo")) options.turbo=get("turbo",options.turbo?"1":"0")=="1";
    if (!args.count("--melodik") && !args.count("--printer") && !args.count("--bt100") && !args.count("--bt100-output")) {
        const auto device=get("printerDevice",get("melodik",options.melodik?"1":"0")=="1"?"melodik":"none");
        options.melodik=device=="melodik";
        if (device=="centronics") options.printer=get("printerFile","");
        options.bt100=device=="bt100"; // Restored BT100 starts with its physical switch off.
    }
    if (!args.count("--mute")) options.mute=get("sound",options.mute?"0":"1")=="0";
    if (!args.count("--scale")) { try { const unsigned n=number(get("scale",std::to_string(options.scale)),8); if (n) options.scale=n; } catch (const std::exception&) {} }
}

struct Desktop::Impl {
    SDL_Window* window; SDL_Renderer* renderer; Machine& m; Options options;
    ImGuiContext* context=NULL;
    Uint32 focusWindow=0;
    std::map<std::string,SDL_Texture*> icons;
    std::map<std::string,ImVec4> bounds;
    std::map<std::string,Uint32> boundWindows;
    SDL_Texture *screen=NULL,*scaled=NULL,*renderTexture=NULL;
    std::vector<std::uint8_t> font;
    std::unique_ptr<ToolWindow> settingsWindow,debuggerWindow;
    enum class Surface { Main,Settings,Debugger };
    Surface fileOwner=Surface::Main,messageOwner=Surface::Main;
    std::vector<std::uint32_t> scaledPixels;
    bool run=true,stop=false,audio=true,fullscreen=false,scanlines=false,scale2x=false;
    bool settings=false,debugger=false,keyboard=false,about=false,settingsWasPaused=false;
    bool printerWindow=false,followPrinter=true;
    int paperZoom=1,lastPaperRow=-1,settingPrinter=0;
    Path printerDestination={}; std::string reportedPrinterError;
    std::uint64_t savedRtcRevision=0;
    unsigned scale=2; int rom=0;
    Path romA={},romB={},sdDirectory={}; std::string activeSd,activeRomA,activeRomB;
    bool settingAudio=true,settingMelodik=false,settingTurbo=false,settingFull=false,settingScan=false,settingScale2x=false,settingSd=false;
    int settingScale=2;
    std::string message,notice,tapePath,recordPath,lastDirectory,config;
    std::vector<std::pair<unsigned,unsigned> > virtualKeys;
    bool followPc=true,physicalMemory=false;
    unsigned assemblyAddress=0,memoryAddress=0x4000;
    std::array<unsigned,6> bpAddresses={{0,0,0,0,0,0}};
    std::array<bool,6> bpEnabled={{false,false,false,false,false,false}};
    std::uint64_t tstateOrigin=0;
    enum class File { None,TapeOpen,TapeSave,SnapshotOpen,SnapshotSave,MemoryOpen,MemorySave,Screenshot,SdDirectory,RomA,RomB,PrinterOutput,Bt100Image };
    File file=File::None;
    bool fileRequest=false,fileWasPaused=false,showHidden=false,headered=true,autorun=true,overwrite=false;
    unsigned loadAddress=0x4000,saveAddress=0x4000,saveSize=0x1000,entryAddress=0x4000;
    bool explicitEntry=false;
    Path browserPath={},fileName={}; std::string fileError,pendingPath;
    std::vector<FileEntry> entries;
    bool quitWithoutSaving=false;

    Impl(SDL_Window* w,SDL_Renderer* r,Machine& machine,const Options& o):window(w),renderer(r),m(machine),options(o) {
        audio=!o.mute; scale=o.scale; activeSd=o.sd; activeRomA=o.romA; activeRomB=o.romB;
        tapePath=o.tape; recordPath=o.record; config=configPath(o);
        savedRtcRevision=m.sd().rtc().revision();
        const auto saved=preferences(o);
        auto flag=[&](const char* name) { const auto p=saved.find(name); return p!=saved.end() && p->second=="1"; };
        scanlines=flag("scanlines"); scale2x=flag("scale2x");
        printerWindow=m.bt100().enabled();
        const auto printerFile=saved.find("printerFile");
        text(printerDestination,!o.printer.empty()?o.printer:printerFile!=saved.end()?printerFile->second:"printer.prn");
        const auto last=saved.find("directory");
        lastDirectory=last!=saved.end() && isDirectory(last->second)?last->second:friendly(canonical("."));
        if (lastDirectory.empty()) lastDirectory=".";
        IMGUI_CHECKVERSION(); context=ImGui::CreateContext(); ImGui::SetCurrentContext(context);
        font=resource("Roboto-Medium.ttf"); configureContext(font); focusWindow=SDL_GetWindowID(window);
        sdl(ImGui_ImplSDL2_InitForSDLRenderer(w,r),"ImGui SDL input");
        sdl(ImGui_ImplSDLRenderer2_Init(r),"ImGui SDL renderer");
        screen=SDL_CreateTexture(r,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,320,256);
        scaled=SDL_CreateTexture(r,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,640,512);
        sdl(screen && scaled,"Create screen textures");
        scaledPixels.resize(640*512);
        for (const char* name:{"reset","pause","run","nmi","open","save","opensn","savesn","binaryopn","binarysav","debugger","settings","keyico","green","yellow","blue","tape","player_play","player_rec","step","stepover","rundbg","stopdbg","ondra","keyboard"}) {
            const auto data=resource(std::string(name)+".rgba"); const unsigned width=dword(data,0),height=dword(data,4);
            if (data.size()!=8+width*height*4) throw std::runtime_error("Invalid UI image");
            SDL_Surface* surface=SDL_CreateRGBSurfaceWithFormatFrom(const_cast<std::uint8_t*>(data.data()+8),width,height,32,width*4,SDL_PIXELFORMAT_RGBA32);
            sdl(surface!=NULL,"Load UI image");
            if (std::string(name)=="ondra") SDL_SetWindowIcon(window,surface);
            SDL_Texture* texture=SDL_CreateTextureFromSurface(r,surface); SDL_FreeSurface(surface);
            sdl(texture!=NULL,"Create UI image"); SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_BLEND); icons[name]=texture;
        }
        if (flag("fullscreen")) setFullscreen(true);
        unsigned slot=0;
        for (auto address:m.breakpoints()) {
            if (slot==bpAddresses.size()) break;
            bpAddresses[slot]=address; bpEnabled[slot++]=true;
        }
    }
    ~Impl() {
        ImGui::SetCurrentContext(context); settingsWindow.reset(); debuggerWindow.reset();
        for (auto& image:icons) SDL_DestroyTexture(image.second);
        SDL_DestroyTexture(screen); SDL_DestroyTexture(scaled);
        ImGui_ImplSDLRenderer2_Shutdown(); ImGui_ImplSDL2_Shutdown(); ImGui::DestroyContext(context);
    }
    Surface surface() const {
        const auto active=ImGui::GetCurrentContext();
        return settingsWindow && active==settingsWindow->context?Surface::Settings:debuggerWindow && active==debuggerWindow->context?Surface::Debugger:Surface::Main;
    }
    SDL_Window* surfaceWindow(Surface owner) const {
        return owner==Surface::Settings && settingsWindow?settingsWindow->window:owner==Surface::Debugger && debuggerWindow?debuggerWindow->window:window;
    }
    void remember(const std::string& name) {
        const auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax(); bounds[name]=ImVec4(a.x,a.y,b.x-a.x,b.y-a.y);
        boundWindows[name]=SDL_GetWindowID(surfaceWindow(surface()));
    }
    void error(const std::string& e) { if (file!=File::None) fileError=e; else { message=e; messageOwner=surface(); } }
    void attempt(const std::function<void()>& action) { try { action(); } catch (const std::exception& e) { error(e.what()); } }
    bool button(const char* name,const char* icon,const char* tooltip,bool selected=false) {
        if (selected) ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(0.58f,0.73f,0.89f,1));
        const auto& images=surface()==Surface::Debugger?debuggerWindow->icons:icons;
        const bool clicked=ImGui::ImageButton(name,imageId(images.at(icon)),ImVec2(18,18));
        if (selected) ImGui::PopStyleColor();
        remember(name);
        tip(tooltip);
        return clicked;
    }
    void savePreferences() {
        if (config.empty()) return;
        std::map<std::string,std::string> values;
        values["rom"]=m.romName(); values["romA"]=activeRomA; values["romB"]=activeRomB; values["sd"]=activeSd;
        values["sdRtc"]=m.sd().rtc().state();
        values["sound"]=audio?"1":"0"; values["melodik"]=m.sound().melodik()?"1":"0";
        values["printerDevice"]=m.printer().enabled()?"centronics":m.bt100().enabled()?"bt100":m.sound().melodik()?"melodik":"none";
        values["printerFile"]=printerDestination.data();
        values["turbo"]=m.turbo()?"1":"0"; values["scale"]=std::to_string(scale);
        values["fullscreen"]=fullscreen?"1":"0"; values["scanlines"]=scanlines?"1":"0"; values["scale2x"]=scale2x?"1":"0";
        values["directory"]=lastDirectory;
        std::string data="# Ondra desktop preferences\n";
        for (const auto& value:values) data+=value.first+"="+escape(value.second)+"\n";
        try { writeFile(config,std::vector<std::uint8_t>(data.begin(),data.end())); }
        catch (const std::exception& e) { error(e.what()); }
    }
    void setFullscreen(bool enabled) {
        if (SDL_SetWindowFullscreen(window,enabled?SDL_WINDOW_FULLSCREEN_DESKTOP:0)!=0) { error(SDL_GetError()); return; }
        fullscreen=enabled;
    }
    void resize(unsigned factor) { scale=factor; if (!fullscreen) SDL_SetWindowSize(window,std::max(640u,320*factor+12),std::max(400u,256*factor+106)); }
    void dialogSize(float width,float height) {
        int w=0,h=0; SDL_GetWindowSize(surfaceWindow(surface()),&w,&h);
        ImGui::SetNextWindowSize(ImVec2(std::min(width,float(w-16)),std::min(height,float(h-16))),ImGuiCond_Appearing);
    }
    void resume() { if (m.breakpoints().count(Z80_PC(m.cpu()))) m.step(); stop=false; }
    void togglePause() { if (stop) resume(); else stop=true; }
    void step() { stop=true; m.step(); followPc=true; }
    void stepOver() {
        stop=true; const auto pc=Z80_PC(m.cpu()); const auto instruction=disassemble(m,pc);
        unsigned pos=pc,op=m.read(static_cast<std::uint16_t>(pos));
        for (unsigned n=0;n<16 && (op==0xdd || op==0xfd);++n) op=m.read(static_cast<std::uint16_t>(++pos));
        const bool call=op==0xcd || (op&0xc7)==0xc4 || (op&0xc7)==0xc7;
        const auto target=static_cast<std::uint16_t>(pc+instruction.length); m.step();
        if (call) {
            unsigned limit=1000000;
            const auto start=m.cycles();
            while (Z80_PC(m.cpu())!=target && !m.breakpoints().count(Z80_PC(m.cpu())) && limit && m.cycles()-start<8000000) { m.step(); --limit; }
            if (Z80_PC(m.cpu())!=target) notice="Step-over stopped at a breakpoint or execution limit.";
        }
        followPc=true;
    }
    void flushRecording() { if (m.tape().recording() && !recordPath.empty()) m.tape().saveRecording(recordPath); }
    void record() {
        if (m.tape().recording()) { flushRecording(); m.tape().stopRecording(); notice="Tape recording saved."; }
        else open(File::TapeSave);
    }
    void quit() {
        try { flushRecording(); run=false; }
        catch (const std::exception& e) { message=e.what(); messageOwner=Surface::Main; SDL_RaiseWindow(window); quitWithoutSaving=true; }
    }
    void openSettings() {
        if (settings) { SDL_RaiseWindow(settingsWindow->window); return; }
        settingsWindow.reset(new ToolWindow(window,font,"Ondra - Settings",660,620,560,520));
        focusWindow=SDL_GetWindowID(settingsWindow->window);
        settingsWasPaused=stop; stop=true; settings=true; rom=romIndex(m.romName());
        text(romA,activeRomA); text(romB,activeRomB); text(sdDirectory,activeSd);
        settingAudio=audio; settingMelodik=m.sound().melodik(); settingTurbo=m.turbo(); settingFull=fullscreen;
        settingPrinter=m.printer().enabled()?1:m.bt100().enabled()?2:0;
        settingScan=scanlines; settingScale2x=scale2x; settingScale=scale; settingSd=!activeSd.empty();
    }
    void openDebugger() {
        if (!debuggerWindow) debuggerWindow.reset(new ToolWindow(window,font,"Ondra - Debugger",960,720,860,660));
        SDL_RaiseWindow(debuggerWindow->window); focusWindow=SDL_GetWindowID(debuggerWindow->window);
        debugger=true; stop=true; followPc=true;
    }
    void closeTool(Surface owner) {
        if (file!=File::None && fileOwner==owner) { file=File::None; stop=fileWasPaused; fileRequest=false; overwrite=false; }
        if (messageOwner==owner) message.clear();
        if (owner==Surface::Settings) { settings=false; stop=settingsWasPaused; }
        else debugger=false; // Closing a debugger preserves Run/Pause.
        SDL_RaiseWindow(window); focusWindow=SDL_GetWindowID(window);
    }
    void open(File mode) {
        if (file!=File::None) { SDL_RaiseWindow(surfaceWindow(fileOwner)); return; }
        fileOwner=surface();
        file=mode; fileRequest=true; fileWasPaused=stop; stop=true; fileError.clear(); pendingPath.clear(); overwrite=false;
        text(browserPath,lastDirectory); text(fileName,mode==File::SnapshotSave?"ondra.osn":mode==File::TapeSave?"recording.csw":mode==File::MemorySave?"memory.bin":mode==File::Screenshot?"screenshot.ppm":mode==File::PrinterOutput?"printer.prn":mode==File::Bt100Image?"bt100.png":"");
        try { refresh(); } catch (const std::exception& e) { entries.clear(); fileError=e.what(); }
    }
    void refresh() {
        const auto path=canonical(browserPath.data());
        if (path.empty() || !isDirectory(path)) throw std::runtime_error("Directory does not exist");
        lastDirectory=friendly(path); text(browserPath,lastDirectory); entries=directory(path,showHidden);
        std::stable_sort(entries.begin(),entries.end(),[](const FileEntry& a,const FileEntry& b) { return a.directory && !b.directory; });
    }
    void accept(const std::string& path);
    void drawMenu(); void drawToolbar(); void drawStatus(); void drawSettings(); void drawDebugger(); void drawKeyboard(); void drawFiles(); void drawMessages(); void drawPrinter();
    void drawTool(ToolWindow& tool,Surface owner);
    void draw(); void render(const std::string& capture);
};

void Desktop::Impl::drawMenu() {
    if (!ImGui::BeginMenuBar()) return;
    const bool f=ImGui::BeginMenu("File"); remember("Menu.File");
    if (f) {
        if (ImGui::MenuItem("Open TAPE for Load...")) open(File::TapeOpen);
        remember("File.OpenTape");
        if (ImGui::MenuItem("Open TAPE for Save...")) open(File::TapeSave);
        if (ImGui::MenuItem("Rewind tape","Home",false,m.tape().loaded())) m.tape().rewind();
        ImGui::Separator();
        if (ImGui::MenuItem("Open snapshot...","F9")) open(File::SnapshotOpen);
        remember("File.OpenSnapshot");
        if (ImGui::MenuItem("Save snapshot...","F8")) open(File::SnapshotSave);
        ImGui::Separator();
        if (ImGui::MenuItem("Load memory block...")) open(File::MemoryOpen);
        if (ImGui::MenuItem("Save memory block...")) open(File::MemorySave);
        ImGui::Separator();
        if (ImGui::MenuItem("Save screenshot...","F12")) open(File::Screenshot);
        if (ImGui::MenuItem("Exit","F10")) quit();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Control")) {
        if (ImGui::MenuItem("Reset","F5")) m.reset(false);
        if (ImGui::MenuItem("Cold reset","Shift+F5")) m.reset();
        if (ImGui::MenuItem(stop?"Run":"Pause","F2")) togglePause();
        if (ImGui::MenuItem("NMI","F4")) m.nmi();
        ImGui::Separator();
        if (ImGui::MenuItem("Turbo 4 MHz","F6",m.turbo())) { m.setTurbo(!m.turbo()); savePreferences(); }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Tools")) {
        if (ImGui::MenuItem("Debugger",NULL,debugger)) openDebugger();
        remember("Tools.Debugger");
        if (ImGui::MenuItem("Settings...")) openSettings();
        remember("Tools.Settings");
        if (ImGui::MenuItem("Keyboard",NULL,keyboard)) keyboard=!keyboard;
        if (ImGui::MenuItem("BT100 paper",NULL,printerWindow,m.bt100().enabled())) printerWindow=!printerWindow;
        remember("Tools.BT100");
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        for (unsigned n=1;n<=4;++n) if (ImGui::MenuItem((std::to_string(n)+"x").c_str(),NULL,scale==n)) { resize(n); savePreferences(); }
        ImGui::Separator();
        if (ImGui::MenuItem("Fullscreen","F11",fullscreen)) { setFullscreen(!fullscreen); savePreferences(); }
        if (ImGui::MenuItem("CRT scanlines",NULL,scanlines)) { scanlines=!scanlines; savePreferences(); }
        if (ImGui::MenuItem("Scale2x",NULL,scale2x)) { scale2x=!scale2x; savePreferences(); }
        ImGui::EndMenu();
    }
    ImGui::SetCursorPosX(ImGui::GetWindowWidth()-65);
    if (ImGui::MenuItem("About")) about=true;
    ImGui::EndMenuBar();
}
void Desktop::Impl::drawToolbar() {
    auto separator=[]() { ImGui::SameLine(0,9); const auto p=ImGui::GetCursorScreenPos(); ImGui::GetWindowDrawList()->AddLine(p,ImVec2(p.x,p.y+25),IM_COL32(160,165,170,255)); ImGui::Dummy(ImVec2(2,25)); ImGui::SameLine(0,9); };
    if (button("Reset","reset","Reset (F5); Shift+F5 clears RAM")) m.reset(false);
    ImGui::SameLine();
    if (button("Pause","pause",stop?"Run (F2)":"Pause (F2)",stop)) togglePause();
    ImGui::SameLine();
    if (button("NMI","nmi","NMI (F4)")) m.nmi();
    separator();
    if (button("Open tape","open","Open tape for Load")) open(File::TapeOpen);
    ImGui::SameLine();
    if (button("Save tape","save","Open tape for Save")) open(File::TapeSave);
    separator();
    if (button("Open snapshot","opensn","Open snapshot (F9)")) open(File::SnapshotOpen);
    ImGui::SameLine();
    if (button("Save snapshot","savesn","Save snapshot (F8)")) open(File::SnapshotSave);
    separator();
    if (button("Load memory","binaryopn","Load binary file into RAM")) open(File::MemoryOpen);
    ImGui::SameLine();
    if (button("Save memory","binarysav","Save memory block")) open(File::MemorySave);
    ImGui::SameLine();
    if (button("Debugger","debugger","Debugger",debugger)) openDebugger();
    separator();
    if (button("Settings","settings","Settings")) openSettings();
    ImGui::SameLine();
    if (button("Keyboard","keyico","Show Ondra keyboard",keyboard)) keyboard=!keyboard;
}
void Desktop::Impl::drawStatus() {
    ImGui::Separator();
    auto led=[&](const char* icon,bool on,const char* help) { ImGui::ImageWithBg(imageId(icons.at(icon)),ImVec2(16,16),ImVec2(0,0),ImVec2(1,1),ImVec4(0,0,0,0),ImVec4(1,1,1,on?1.0f:0.22f)); tip(help); ImGui::SameLine(); };
    led("green",!(m.portA0()&1),"Green LED"); led("yellow",!(m.portA0()&2),"Yellow LED"); led("tape",m.tape().motor(),"Tape motor");
    if (button("Record",m.tape().recording()?"player_rec":"player_play","Play / Record",m.tape().recording())) record();
    ImGui::SameLine();
    ImGui::TextUnformatted(stop?"Paused":"Running"); if (!notice.empty()) tip(notice.c_str());
    ImGui::SameLine(); ImGui::Text("| %s | %s",m.turbo()?"4 MHz":"2 MHz",romNames[romIndex(m.romName())]);
    if (m.tape().loaded()) { ImGui::SameLine(); ImGui::SetNextItemWidth(75); ImGui::ProgressBar(static_cast<float>(m.tape().progress()),ImVec2(75,14),m.tape().finished()?"End":"Tape"); tip(tapePath.c_str()); }
    if (m.sd().enabled()) { ImGui::SameLine(); ImGui::TextUnformatted("| SD"); tip((activeSd+"\nRTC: "+m.sd().rtc().text()).c_str()); }
    if (m.printer().enabled()) { ImGui::SameLine(); ImGui::Text("| PRN: %llu",static_cast<unsigned long long>(m.printer().total())); tip(m.printer().path().c_str()); }
}
void Desktop::Impl::draw() {
    ImGui::SetCurrentContext(context);
    if (!settings) settingsWindow.reset();
    if (!debugger) debuggerWindow.reset();
    if (SDL_GetKeyboardFocus()==window) ImGui::GetIO().ConfigFlags&=~ImGuiConfigFlags_NoMouseCursorChange;
    else ImGui::GetIO().ConfigFlags|=ImGuiConfigFlags_NoMouseCursorChange;
    ImGui_ImplSDLRenderer2_NewFrame(); ImGui_ImplSDL2_NewFrame(); ImGui::NewFrame(); bounds.clear(); boundWindows.clear(); virtualKeys.clear();
    ImGui::SetNextWindowPos(ImVec2(0,0)); ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(fullscreen?0:6,fullscreen?0:6));
    const auto flags=ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoBringToFrontOnFocus|(fullscreen?0:ImGuiWindowFlags_MenuBar);
    ImGui::Begin("Ondra desktop",NULL,flags);
    const bool blocked=settings || (file!=File::None && fileOwner!=Surface::Main) || (!message.empty() && messageOwner!=Surface::Main);
    ImGui::BeginDisabled(blocked);
    attempt([&]() { if (!fullscreen) { drawMenu(); drawToolbar(); } });
    const float statusHeight=fullscreen?0:40;
    const auto available=ImGui::GetContentRegionAvail(); const ImVec2 area(std::max(1.0f,available.x),std::max(1.0f,available.y-statusHeight));
    const auto start=ImGui::GetCursorScreenPos(); ImGui::GetWindowDrawList()->AddRectFilled(start,ImVec2(start.x+area.x,start.y+area.y),IM_COL32(0,0,0,255));
    const float ratio=std::min(area.x/320,area.y/256); const ImVec2 imageSize(320*ratio,256*ratio);
    const ImVec2 imagePos(start.x+(area.x-imageSize.x)/2,start.y+(area.y-imageSize.y)/2);
    renderTexture=scale2x?scaled:screen;
    ImGui::GetWindowDrawList()->AddImage(imageId(renderTexture),imagePos,ImVec2(imagePos.x+imageSize.x,imagePos.y+imageSize.y),ImVec2(0,0),ImVec2(1,1),scanlines?IM_COL32(225,225,225,255):IM_COL32_WHITE);
    if (scanlines && ratio>=2) for (unsigned row=0;row<256;++row) {
        const float y=imagePos.y+row*ratio;
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(imagePos.x,y),ImVec2(imagePos.x+imageSize.x,y+ratio*0.15f),IM_COL32_BLACK);
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(imagePos.x,y+ratio*0.85f),ImVec2(imagePos.x+imageSize.x,y+ratio),IM_COL32_BLACK);
    }
    ImGui::InvisibleButton("Screen",area); remember("Screen");
    if (!fullscreen) attempt([&]() { drawStatus(); });
    ImGui::EndDisabled(); ImGui::End(); ImGui::PopStyleVar();
    ImGui::BeginDisabled(blocked); attempt([&]() { drawKeyboard(); drawPrinter(); }); ImGui::EndDisabled();
    attempt([&]() { drawFiles(); });
    if (!m.printer().error().empty() && reportedPrinterError!=m.printer().error()) { reportedPrinterError=m.printer().error(); error(reportedPrinterError); }
    drawMessages();
    if (settingsWindow && settings) drawTool(*settingsWindow,Surface::Settings);
    if (debuggerWindow && debugger) drawTool(*debuggerWindow,Surface::Debugger);
    // SDL mouse capture is global. The backend for an unfocused context must
    // not cancel a drag in the focused window when its NewFrame runs later.
    ImGuiContext* focused=context;
    if (settingsWindow && SDL_GetKeyboardFocus()==settingsWindow->window) focused=settingsWindow->context;
    if (debuggerWindow && SDL_GetKeyboardFocus()==debuggerWindow->window) focused=debuggerWindow->context;
    { ContextScope active(focused); bool dragging=false;
      for (int button=0;button<3;++button) dragging=dragging || ImGui::IsMouseDragging(button,1.0f);
      SDL_CaptureMouse(dragging?SDL_TRUE:SDL_FALSE); }
    ImGui::SetCurrentContext(context);
}
void Desktop::Impl::drawTool(ToolWindow& tool,Surface owner) {
    ContextScope restore(tool.context); tool.begin();
    ImGui::BeginDisabled((owner==Surface::Debugger && settings) || (file!=File::None && fileOwner!=owner) || (!message.empty() && messageOwner!=owner));
    if (owner==Surface::Settings) drawSettings(); else drawDebugger();
    ImGui::EndDisabled();
    attempt([&]() { drawFiles(); }); drawMessages();
}
void Desktop::Impl::render(const std::string& capture) {
    ContextScope active(context);
    if (m.sd().rtc().revision()!=savedRtcRevision) { savedRtcRevision=m.sd().rtc().revision(); savePreferences(); }
    const auto& pixels=m.pixels();
    if (renderTexture==scaled) {
        for (unsigned y=0;y<256;++y) for (unsigned x=0;x<320;++x) {
            const auto e=pixels[y*320+x],b=pixels[(y?y-1:y)*320+x],h=pixels[(y<255?y+1:y)*320+x];
            const auto d=pixels[y*320+(x?x-1:x)],f=pixels[y*320+(x<319?x+1:x)];
            const unsigned pos=y*2*640+x*2; const bool edge=b!=h && d!=f;
            scaledPixels[pos]=edge && d==b?d:e; scaledPixels[pos+1]=edge && b==f?f:e;
            scaledPixels[pos+640]=edge && d==h?d:e; scaledPixels[pos+641]=edge && h==f?f:e;
        }
        sdl(SDL_UpdateTexture(scaled,NULL,scaledPixels.data(),640*4)==0,"Scale2x texture");
    } else sdl(SDL_UpdateTexture(screen,NULL,pixels.data(),320*4)==0,"Screen texture");
    ImGui::Render(); SDL_SetRenderDrawColor(renderer,0,0,0,255); SDL_RenderClear(renderer);
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(),renderer);
    if (!capture.empty()) {
        int width=0,height=0; SDL_GetRendererOutputSize(renderer,&width,&height);
        SDL_Surface* surface=SDL_CreateRGBSurfaceWithFormat(0,width,height,32,SDL_PIXELFORMAT_ARGB8888); sdl(surface!=NULL,"UI screenshot");
        const int read=SDL_RenderReadPixels(renderer,NULL,surface->format->format,surface->pixels,surface->pitch);
        const int saved=read==0?SDL_SaveBMP(surface,capture.c_str()):-1; SDL_FreeSurface(surface); sdl(read==0 && saved==0,"Save UI screenshot");
    }
    SDL_RenderPresent(renderer);
    if (settingsWindow) settingsWindow->render();
    if (debuggerWindow) debuggerWindow->render();
}

void Desktop::Impl::accept(const std::string& path) {
    switch (file) {
    case File::TapeOpen: flushRecording(); m.tape().load(path); tapePath=path; recordPath.clear(); break;
    case File::TapeSave: flushRecording(); recordPath=path; m.tape().startRecording(); break;
    case File::SnapshotOpen: m.loadSnapshot(path); followPc=true; break;
    case File::SnapshotSave: m.saveSnapshot(path); break;
    case File::MemoryOpen:
        if (loadAddress>65535 || entryAddress>65535) throw std::runtime_error("Address must be 0000..FFFF");
        if (headered) m.loadBinary(path,autorun); else m.loadRaw(path,loadAddress);
        if (autorun && (explicitEntry || !headered)) m.jump(static_cast<std::uint16_t>(explicitEntry?entryAddress:loadAddress));
        break;
    case File::MemorySave:
        if (saveAddress>65535 || !saveSize || saveSize>65536-saveAddress || entryAddress>65535) throw std::runtime_error("Memory range must fit in 64 KiB");
        if (headered) m.saveBinary(path,saveAddress,saveSize,explicitEntry?static_cast<int>(entryAddress):-1);
        else writeFile(path,std::vector<std::uint8_t>(m.ram().begin()+saveAddress,m.ram().begin()+saveAddress+saveSize));
        break;
    case File::Screenshot: m.screenshot(path); break;
    case File::SdDirectory: text(sdDirectory,friendly(path)); settingSd=true; break;
    case File::RomA: text(romA,friendly(path)); break;
    case File::RomB: text(romB,friendly(path)); break;
    case File::PrinterOutput: text(printerDestination,friendly(path)); break;
    case File::Bt100Image: m.bt100().savePng(path); break;
    default: break;
    }
    notice=baseName(path); file=File::None; stop=fileWasPaused;
    ImGui::CloseCurrentPopup(); savePreferences();
}
void Desktop::Impl::drawFiles() {
    if (file==File::None || fileOwner!=surface()) return;
    if (fileRequest) { ImGui::OpenPopup("Select file"); fileRequest=false; }
    dialogSize(660,500);
    bool visible=true;
    if (ImGui::BeginPopupModal("Select file",&visible,ImGuiWindowFlags_NoSavedSettings)) {
        const bool folder=file==File::SdDirectory;
        const bool save=file==File::TapeSave || file==File::SnapshotSave || file==File::MemorySave || file==File::Screenshot || file==File::PrinterOutput || file==File::Bt100Image;
        const char* title=file==File::TapeOpen?"Open tape (TAP / CSW / WAV)":file==File::TapeSave?"Record tape to CSW":file==File::SnapshotOpen?"Open OSN snapshot":file==File::SnapshotSave?"Save OSN snapshot":file==File::MemoryOpen?"Load memory block":file==File::MemorySave?"Save memory block":file==File::Screenshot?"Save emulated screen (PPM)":file==File::PrinterOutput?"Centronics output (raw bytes appended to file)":file==File::Bt100Image?"Save BT100 paper (PNG image)":folder?"Select SD card directory":"Select ROM bank";
        ImGui::TextUnformatted(title); ImGui::Separator();
        if (ImGui::Button("Up")) { text(browserPath,parent(browserPath.data())); attempt([&]() { refresh(); }); } remember("File.Up");
        ImGui::SameLine(); ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##directory",browserPath.data(),browserPath.size(),ImGuiInputTextFlags_EnterReturnsTrue)) attempt([&]() { refresh(); });
        remember("File.Directory");
        if (ImGui::Checkbox("Show hidden files",&showHidden)) attempt([&]() { refresh(); });
        ImGui::SameLine(); if (ImGui::Button("Refresh")) attempt([&]() { refresh(); });
        bool chosen=false;
        const bool memory=file==File::MemoryOpen || file==File::MemorySave;
        ImGui::BeginChild("Files",ImVec2(0,ImGui::GetContentRegionAvail().y-(memory?170:100)),ImGuiChildFlags_Borders);
        if (ImGui::Selectable("[..]",false,ImGuiSelectableFlags_AllowDoubleClick) && ImGui::IsMouseDoubleClicked(0)) { text(browserPath,parent(browserPath.data())); attempt([&]() { refresh(); }); }
        std::string nextDirectory;
        for (std::size_t i=0;i<entries.size();++i) {
            const auto& entry=entries[i];
            if (folder && !entry.directory) continue;
            const auto ext=extension(entry.name);
            if (!entry.directory && file==File::TapeOpen && ext!=".tap" && ext!=".csw" && ext!=".wav") continue;
            if (!entry.directory && file==File::SnapshotOpen && ext!=".osn") continue;
            const std::string label=entry.directory?"[ "+entry.name+" ]":entry.name;
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Selectable(label.c_str(),entry.name==fileName.data(),ImGuiSelectableFlags_AllowDoubleClick)) {
                if (!entry.directory) text(fileName,entry.name);
                if (ImGui::IsMouseDoubleClicked(0)) {
                    if (entry.directory) nextDirectory=friendly(entry.path); else chosen=true;
                }
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
        if (!nextDirectory.empty()) { text(browserPath,nextDirectory); text(fileName,""); attempt([&]() { refresh(); }); }
        if (!folder) { ImGui::SetNextItemWidth(-85); if (ImGui::InputText("File name",fileName.data(),fileName.size(),ImGuiInputTextFlags_EnterReturnsTrue)) chosen=true; remember("File.Name"); }
        if (memory) {
            ImGui::Checkbox("OndraSD BIN headers",&headered); ImGui::SameLine(); ImGui::Checkbox("Entry point",&explicitEntry);
            if (file==File::MemoryOpen) {
                ImGui::Checkbox("Run after load",&autorun); ImGui::SameLine(); ImGui::SetNextItemWidth(85); hexInput("Raw address",loadAddress);
            } else {
                ImGui::SetNextItemWidth(85); hexInput("Start",saveAddress); ImGui::SameLine(); ImGui::SetNextItemWidth(85); hexInput("Size (hex)",saveSize);
            }
            if (explicitEntry) { ImGui::SetNextItemWidth(85); hexInput("PC",entryAddress); }
        }
        if (!fileError.empty()) ImGui::TextColored(ImVec4(0.75f,0.1f,0.1f,1),"%s",fileError.c_str());
        if (ImGui::Button(folder?"Select folder":save?"Save":"Open",ImVec2(100,0))) chosen=true;
        remember("File.Accept");
        ImGui::SameLine();
        if (ImGui::Button("Cancel",ImVec2(100,0))) { visible=false; ImGui::CloseCurrentPopup(); } remember("File.Cancel");
        if (chosen) {
            fileError.clear();
            std::string path=folder?std::string(browserPath.data()):std::string(browserPath.data())+"/"+fileName.data();
            if (file==File::Bt100Image && extension(path)!=".png") path+=".png";
            if (!folder && fileName[0]=='\0') fileError="Enter a file name.";
            else if (!folder && isDirectory(path)) { text(browserPath,friendly(canonical(path))); text(fileName,""); attempt([&]() { refresh(); }); }
            else if (!save && canonical(path).empty()) fileError="Select an existing file or directory.";
            else if (save && file!=File::PrinterOutput && !canonical(path).empty()) { pendingPath=path; overwrite=true; ImGui::OpenPopup("Replace file?"); }
            else { try { accept(path); } catch (const std::exception& e) { fileError=e.what(); } }
        }
        bool replace=false;
        if (overwrite && ImGui::BeginPopupModal("Replace file?",NULL,ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("The file already exists. Replace it?");
            ImGui::TextUnformatted(baseName(pendingPath).c_str());
            if (ImGui::Button("Replace")) { replace=true; overwrite=false; ImGui::CloseCurrentPopup(); }
            remember("File.Replace");
            ImGui::SameLine(); if (ImGui::Button("Cancel")) { overwrite=false; ImGui::CloseCurrentPopup(); }
            remember("File.Keep");
            ImGui::EndPopup();
        }
        if (replace) { try { accept(pendingPath); } catch (const std::exception& e) { fileError=e.what(); } }
        ImGui::EndPopup();
    }
    if (!visible) { file=File::None; stop=fileWasPaused; }
}

void Desktop::Impl::drawSettings() {
    if (!settings) return;
    ImGui::SetNextWindowPos(ImVec2(0,0)); ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("Settings",NULL,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings);
    if (ImGui::BeginTabBar("Settings tabs")) {
        if (ImGui::BeginTabItem("Computer")) {
            ImGui::SeparatorText("ROM");
            for (int n=0;n<6;++n) { if (n && n%2) ImGui::SameLine(260); ImGui::RadioButton(romNames[n],&rom,n); }
            if (rom==5) {
                ImGui::SetNextItemWidth(-55); ImGui::InputText("##romA",romA.data(),romA.size()); ImGui::SameLine(); if (ImGui::Button("A...")) open(File::RomA);
                ImGui::SetNextItemWidth(-55); ImGui::InputText("##romB",romB.data(),romB.size()); ImGui::SameLine(); if (ImGui::Button("B...")) open(File::RomB);
                ImGui::TextDisabled("Each ROM bank: 2, 4 or 8 KiB");
            }
            ImGui::SeparatorText("Sound and clock");
            ImGui::Checkbox("Sound",&settingAudio); ImGui::SameLine(260);
            if (ImGui::Checkbox("Melodik module",&settingMelodik) && settingMelodik) settingPrinter=0;
            ImGui::Checkbox("Turbo 4 MHz",&settingTurbo);
            ImGui::SeparatorText("Display");
            ImGui::Checkbox("Fullscreen",&settingFull); ImGui::SameLine(260); ImGui::Checkbox("CRT scanlines",&settingScan);
            ImGui::Checkbox("Scale2x smoothing",&settingScale2x); ImGui::SameLine(260); ImGui::SetNextItemWidth(130); ImGui::SliderInt("Zoom",&settingScale,1,4,"%dx");
            ImGui::SeparatorText("OndraSD");
            ImGui::Checkbox("Enable OndraSD module",&settingSd);
            ImGui::SetNextItemWidth(-85); ImGui::InputText("##sdpath",sdDirectory.data(),sdDirectory.size()); ImGui::SameLine(); if (ImGui::Button("Browse...")) open(File::SdDirectory);
            remember("Settings.SdBrowse");
            ImGui::Spacing(); ImGui::TextWrapped("Changing ROM or the SD card resets the computer. Sound and display changes apply without a reset.");
            ImGui::EndTabItem();
        }
        const bool printersTab=ImGui::BeginTabItem("Printers"); remember("Settings.Printers");
        if (printersTab) {
            ImGui::TextWrapped("Printers and the Melodik module share one connector. Connecting a printer disables Melodik.");
            ImGui::Spacing();
            ImGui::RadioButton("No printer",&settingPrinter,0); remember("Settings.NoPrinter");
            if (ImGui::RadioButton("Centronics printer",&settingPrinter,1)) settingMelodik=false;
            remember("Settings.Centronics");
            if (ImGui::RadioButton("Tesla BT100",&settingPrinter,2)) settingMelodik=false;
            remember("Settings.BT100");
            if (settingPrinter==1) {
                ImGui::Spacing(); ImGui::TextUnformatted("Output file (raw bytes, appended):");
                ImGui::SetNextItemWidth(-90); ImGui::InputText("##printerfile",printerDestination.data(),printerDestination.size()); remember("Settings.PrinterFile");
                ImGui::SameLine(); if (ImGui::Button("Browse...")) open(File::PrinterOutput);
                ImGui::TextWrapped("The printer accepts data at the falling STROBE edge and reports BUSY while its buffer is full.");
            } else if (settingPrinter==2) {
                ImGui::Spacing(); ImGui::TextWrapped("The paper window provides a power switch, paper preview and PNG export. Power on when the program requests it; powered sensors affect the Ondra keyboard.");
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::Spacing();
    if (ImGui::Button("Apply",ImVec2(100,0))) attempt([&]() {
        const std::string nextSd=settingSd?std::string(sdDirectory.data()):"";
        if (settingSd && (nextSd.empty() || !isDirectory(nextSd))) throw std::runtime_error("Select an existing SD card directory.");
        const std::string nextPrinter=settingPrinter==1?std::string(printerDestination.data()):"";
        if (settingPrinter==1 && (nextPrinter.empty() || isDirectory(nextPrinter))) throw std::runtime_error("Select a file for Centronics output.");
        const bool changeRom=m.romName()!=romIds[rom] || (rom==5 && (activeRomA!=romA.data() || activeRomB!=romB.data()));
        if (changeRom) { if (rom==5) m.customRom(romA.data(),romB.data()); else m.selectRom(romIds[rom]); activeRomA=romA.data(); activeRomB=romB.data(); }
        if (nextSd!=activeSd) { m.sd().configure(nextSd); m.reset(false); activeSd=nextSd; }
        if (nextPrinter!=m.printer().path() || (settingPrinter==1 && !m.printer().enabled())) { m.printer().configure(nextPrinter); reportedPrinterError.clear(); }
        const bool nextBt=settingPrinter==2;
        if (nextBt && !m.bt100().enabled()) printerWindow=true;
        if (!nextBt && m.bt100().enabled()) m.bt100().power(false,m.cycles(),m.turbo()?2:1);
        m.bt100().configure(nextBt);
        audio=settingAudio; m.sound().setMelodik(settingMelodik && settingPrinter==0); m.setTurbo(settingTurbo);
        scanlines=settingScan; scale2x=settingScale2x;
        if (settingFull!=fullscreen) setFullscreen(settingFull);
        if (settingScale!=static_cast<int>(scale)) resize(settingScale);
        notice="Settings applied."; closeTool(Surface::Settings);
        { ContextScope active(context); savePreferences(); }
    }); remember("Settings.Apply");
    ImGui::SameLine(); if (ImGui::Button("Cancel",ImVec2(100,0))) closeTool(Surface::Settings); remember("Settings.Cancel");
    if (!config.empty()) { ImGui::Spacing(); ImGui::TextDisabled("Settings: %s",friendly(config).c_str()); }
    ImGui::End();
}

void Desktop::Impl::drawPrinter() {
    if (!printerWindow || !m.bt100().enabled()) return;
    auto& printer=m.bt100(); printer.advance(m.cycles(),m.turbo()?2:1);
    dialogSize(700,440);
    ImGui::Begin("Tesla BT100 - paper",&printerWindow,ImGuiWindowFlags_NoCollapse);
    if (ImGui::Button(printer.powered()?"Power off":"Power on")) printer.power(!printer.powered(),m.cycles(),m.turbo()?2:1);
    remember("Printer.Power"); tip("Power on only when ready to print: BT100 sensors share the keyboard lines.");
    ImGui::SameLine(); if (ImGui::Button("Save PNG...")) open(File::Bt100Image); remember("Printer.Save");
    ImGui::SameLine(); if (ImGui::Button("New paper")) { printer.newPaper(); lastPaperRow=-1; } remember("Printer.New");
    ImGui::SameLine(); ImGui::Checkbox("Follow print",&followPrinter);
    ImGui::Text("%s | Row: %u | Carriage: %.1f",printer.powered()?"On":"Off",printer.row(),printer.carriage());
    ImGui::SameLine(); ImGui::SetNextItemWidth(100); ImGui::SliderInt("Zoom",&paperZoom,1,3,"%dx");
    if (printer.paperFull()) ImGui::TextColored(ImVec4(0.75f,0.1f,0.1f,1),"Paper full. Save it and insert new paper to continue.");
    ImGui::BeginChild("Paper",ImVec2(0,0),ImGuiChildFlags_Borders,ImGuiWindowFlags_HorizontalScrollbar);
    const auto cursor=ImGui::GetCursorScreenPos(); const ImVec2 origin(cursor.x+12,cursor.y+12);
    const float localY=ImGui::GetCursorPosY()+12,zoom=static_cast<float>(paperZoom);
    const unsigned rows=std::max(300u,printer.paperRows());
    auto* draw=ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin,ImVec2(origin.x+Bt100::Width*zoom,origin.y+rows*zoom),IM_COL32(250,248,238,255));
    const unsigned first=static_cast<unsigned>(std::max(0.0f,std::floor((ImGui::GetScrollY()-12)/zoom)));
    const unsigned last=std::min(rows,first+static_cast<unsigned>(ImGui::GetWindowHeight()/zoom)+3);
    for (unsigned row=first;row<last;++row) for (unsigned col=0;col<Bt100::Width;++col) if (printer.dot(row,col)) {
        const ImVec2 point(origin.x+col*zoom,origin.y+row*zoom);
        draw->AddRectFilled(point,ImVec2(point.x+zoom,point.y+zoom),IM_COL32(40,40,60,255));
    }
    const float headX=origin.x+static_cast<float>(printer.carriage())*zoom,headY=origin.y+printer.row()*zoom;
    draw->AddRect(ImVec2(headX-3,headY-6),ImVec2(headX+4,headY+8),IM_COL32(210,40,40,255));
    draw->AddLine(ImVec2(origin.x,headY+zoom+1),ImVec2(origin.x+Bt100::Width*zoom,headY+zoom+1),IM_COL32(230,50,50,70));
    ImGui::Dummy(ImVec2(Bt100::Width*zoom+24,rows*zoom+24));
    if (followPrinter && lastPaperRow!=static_cast<int>(printer.row())) ImGui::SetScrollFromPosY(localY+printer.row()*zoom,0.5f);
    lastPaperRow=static_cast<int>(printer.row());
    ImGui::EndChild(); ImGui::End();
}

void Desktop::Impl::drawMessages() {
    if (about && surface()==Surface::Main) {
        ImGui::SetNextWindowSize(ImVec2(390,0),ImGuiCond_Appearing);
        ImGui::Begin("About Ondra",&about,ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoCollapse);
        ImGui::Image(imageId(icons.at("ondra")),ImVec2(48,48)); ImGui::SameLine();
        ImGui::TextUnformatted("Ondra SPO 186\nC++11 port of JOndra");
        ImGui::Separator();
        ImGui::Text("Version: %s",ONDRA_VERSION);
        ImGui::TextUnformatted("Built: " __DATE__ " " __TIME__);
        ImGui::Separator(); ImGui::TextUnformatted("Original emulator: omikron88/jondra\nCPU: redcode/Z80\nDesktop: SDL2 + Dear ImGui");
        ImGui::TextUnformatted("GPL-3.0-or-later; third-party licenses included.");
        ImGui::TextWrapped("Keyboard: Shift, Alt=Symbols, Ctrl, Tab=Numbers, Equals=CS. F11 toggles fullscreen.");
        if (ImGui::Button("Close")) about=false;
        ImGui::End();
    }
    if (messageOwner!=surface()) return;
    if (!message.empty()) ImGui::OpenPopup("Ondra message");
    ImGui::SetNextWindowSize(ImVec2(440,0),ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Ondra message",NULL,ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s",message.c_str());
        if (ImGui::Button("OK",ImVec2(100,0))) { message.clear(); ImGui::CloseCurrentPopup(); } remember("Message.OK");
        if (quitWithoutSaving) { ImGui::SameLine(); if (ImGui::Button("Quit without saving")) { m.tape().stopRecording(); message.clear(); run=false; ImGui::CloseCurrentPopup(); } }
        ImGui::EndPopup();
    }
}

void Desktop::Impl::drawKeyboard() {
    if (!keyboard) return;
    dialogSize(640,385);
    ImGui::Begin("Ondra keyboard",&keyboard,ImGuiWindowFlags_NoCollapse);
    const float width=std::min(600.0f,ImGui::GetContentRegionAvail().x);
    ImGui::Image(imageId(icons.at("keyboard")),ImVec2(width,width*169/600));
    ImGui::TextDisabled("Click and hold a key; use the PC keyboard for combinations.");
    struct Key { const char* label; unsigned row,bit; };
    const Key keys[]={
        {"Q",0,4},{"W",0,2},{"E",0,1},{"R",0,0},{"T",0,3},{"Y",6,3},{"U",6,0},{"I",6,1},{"O",6,2},{"P",6,4},
        {"A",1,4},{"S",1,2},{"D",1,1},{"F",1,0},{"G",1,3},{"H",5,3},{"J",5,0},{"K",5,1},{"L",5,2},{"Enter",5,4},
        {"Z",2,2},{"X",2,1},{"C",2,0},{"V",2,3},{"B",7,3},{"N",7,0},{"M",7,1},{"Up",7,2},{"Left",8,1},{"Right",8,4},
        {"Shift",4,4},{"Symbols",2,4},{"Ctrl",7,4},{"Numbers",4,1},{"CS",4,2},{"Space",3,0},{"Down",8,2}
    };
    for (unsigned n=0;n<sizeof(keys)/sizeof(keys[0]);++n) {
        if (n%10) ImGui::SameLine(0,3);
        ImGui::Button(keys[n].label,ImVec2(n<30?49:75,24));
        if (ImGui::IsItemActive() && ImGui::IsMouseDown(0)) virtualKeys.push_back(std::make_pair(keys[n].row,keys[n].bit));
    }
    ImGui::End();
}

void Desktop::Impl::drawDebugger() {
    if (!debugger) return;
    ImGui::SetNextWindowPos(ImVec2(0,0)); ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("Debugger",NULL,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings);
    if (button("Debug.Run","rundbg","Run / Pause",!stop)) togglePause();
    ImGui::SameLine();
    if (button("Debug.Step","step","Step into (F3)")) attempt([&]() { step(); });
    ImGui::SameLine();
    if (button("Debug.Over","stepover","Step over (F7)")) attempt([&]() { stepOver(); });
    ImGui::SameLine();
    if (button("Debug.Reset","reset","Reset")) m.reset(false);
    ImGui::SameLine();
    ImGui::Text("%s | T-states: %llu",stop?"Paused":"Running",static_cast<unsigned long long>(m.cycles()-std::min(tstateOrigin,m.cycles())));
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) tstateOrigin=m.cycles();
    Z80& c=m.cpu();
    if (ImGui::BeginTable("Top",2,ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("Assembly",ImGuiTableColumnFlags_WidthStretch,0.6f); ImGui::TableSetupColumn("Registers",ImGuiTableColumnFlags_WidthStretch,0.4f);
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(80); if (hexInput("Address",assemblyAddress)) { assemblyAddress&=65535; followPc=false; } ImGui::SameLine(); ImGui::Checkbox("Follow PC",&followPc);
        if (followPc) assemblyAddress=Z80_PC(m.cpu());
        ImGui::BeginChild("Disassembly",ImVec2(0,245),ImGuiChildFlags_Borders);
        if (ImGui::BeginTable("Instructions",3,ImGuiTableFlags_SizingFixedFit)) {
            // Spaces in the proportional UI font cannot align byte strings.
            // Reserve fixed columns for the address and up to four bytes.
            float hexWidth=0;
            for (const char* digit="0123456789ABCDEF";*digit;++digit) hexWidth=std::max(hexWidth,ImGui::CalcTextSize(digit,digit+1).x);
            const float space=ImGui::CalcTextSize(" ").x;
            ImGui::TableSetupColumn("Address",ImGuiTableColumnFlags_WidthFixed,4*hexWidth+ImGui::CalcTextSize("* ").x);
            ImGui::TableSetupColumn("Bytes",ImGuiTableColumnFlags_WidthFixed,8*hexWidth+3*space);
            ImGui::TableSetupColumn("Instruction",ImGuiTableColumnFlags_WidthStretch);
            unsigned address=assemblyAddress;
            for (unsigned n=0;n<14;++n) {
                const auto a=static_cast<std::uint16_t>(address); const auto instruction=disassemble(m,a);
                char label[16],bytes[30]={};
                for (unsigned i=0;i<std::min(4u,instruction.length);++i) std::snprintf(bytes+i*3,sizeof(bytes)-i*3,"%02X ",m.read(static_cast<std::uint16_t>(a+i)));
                std::snprintf(label,sizeof(label),"%c %04X",m.breakpoints().count(a)?'*':' ',a);
                ImGui::TableNextRow(); ImGui::TableNextColumn();
                if (ImGui::Selectable(label,a==Z80_PC(m.cpu()),ImGuiSelectableFlags_SpanAllColumns)) {
                    if (m.breakpoints().erase(a)) {
                        for (unsigned i=0;i<bpAddresses.size();++i) if (bpAddresses[i]==a) bpEnabled[i]=false;
                    } else {
                        m.breakpoints().insert(a);
                        for (unsigned i=0;i<bpAddresses.size();++i) if (!bpEnabled[i]) { bpAddresses[i]=a; bpEnabled[i]=true; break; }
                    }
                }
                ImGui::TableNextColumn(); ImGui::TextUnformatted(bytes);
                ImGui::TableNextColumn(); ImGui::TextUnformatted(instruction.text.c_str());
                address=(address+instruction.length)&65535;
            }
            ImGui::EndTable();
        }
        ImGui::EndChild();
        ImGui::TableNextColumn();
        ImGui::BeginDisabled(!stop);
        auto reg=[&](const char* label,std::uint16_t value,bool pc) {
            unsigned edited=value;
            ImGui::SetNextItemWidth(70);
            const bool committed=hexInput(label,edited) && edited<=65535;
            remember(std::string("Debug.")+label);
            if (committed && pc) m.jump(static_cast<std::uint16_t>(edited));
            return committed?static_cast<std::uint16_t>(edited):value;
        };
        Z80_PC(c)=reg("PC",Z80_PC(c),true); ImGui::SameLine(); Z80_SP(c)=reg("SP",Z80_SP(c),false);
        Z80_AF(c)=reg("AF",Z80_AF(c),false); ImGui::SameLine(); Z80_AF_(c)=reg("AF'",Z80_AF_(c),false);
        Z80_BC(c)=reg("BC",Z80_BC(c),false); ImGui::SameLine(); Z80_BC_(c)=reg("BC'",Z80_BC_(c),false);
        Z80_DE(c)=reg("DE",Z80_DE(c),false); ImGui::SameLine(); Z80_DE_(c)=reg("DE'",Z80_DE_(c),false);
        Z80_HL(c)=reg("HL",Z80_HL(c),false); ImGui::SameLine(); Z80_HL_(c)=reg("HL'",Z80_HL_(c),false);
        Z80_IX(c)=reg("IX",Z80_IX(c),false); ImGui::SameLine(); Z80_IY(c)=reg("IY",Z80_IY(c),false);
        unsigned i=c.i,r=c.r;
        ImGui::SetNextItemWidth(45); if (hexInput("I",i,2) && i<=255) c.i=static_cast<std::uint8_t>(i); ImGui::SameLine();
        ImGui::SetNextItemWidth(45); if (hexInput("R",r,2) && r<=255) c.r=static_cast<std::uint8_t>(r);
        bool iff=c.iff1!=0; if (ImGui::Checkbox("IFF1",&iff)) { c.iff1=iff; z80_int(&c,c.int_line); } ImGui::SameLine();
        int mode=c.im; ImGui::SetNextItemWidth(90); if (ImGui::Combo("IM",&mode,"0\0" "1\0" "2\0")) c.im=static_cast<std::uint8_t>(mode);
        ImGui::EndDisabled();
        ImGui::Text("Flags: %c%c%c%c%c%c%c%c",Z80_F(c)&128?'S':'-',Z80_F(c)&64?'Z':'-',Z80_F(c)&32?'5':'-',Z80_F(c)&16?'H':'-',Z80_F(c)&8?'3':'-',Z80_F(c)&4?'P':'-',Z80_F(c)&2?'N':'-',Z80_F(c)&1?'C':'-');
        ImGui::EndTable();
    }
    ImGui::SeparatorText("Execution breakpoints (or click an assembly line)");
    for (unsigned n=0;n<6;++n) {
        if (n) ImGui::SameLine();
        ImGui::PushID(static_cast<int>(n));
        const unsigned previous=bpAddresses[n];
        bool changed=ImGui::Checkbox("##enabled",&bpEnabled[n]); remember("Debug.BreakEnabled"+std::to_string(n));
        ImGui::SameLine(); ImGui::SetNextItemWidth(63);
        unsigned edited=bpAddresses[n];
        if (hexInput("##address",edited)) { bpAddresses[n]=edited&65535; changed=true; }
        remember("Debug.BreakAddress"+std::to_string(n));
        if (changed) {
            m.breakpoints().erase(static_cast<std::uint16_t>(previous));
            for (unsigned i=0;i<bpAddresses.size();++i) if (bpEnabled[i]) m.breakpoints().insert(static_cast<std::uint16_t>(bpAddresses[i]));
        }
        ImGui::PopID();
    }
    ImGui::SeparatorText("Memory"); ImGui::SetNextItemWidth(85); hexInput("Start",memoryAddress); memoryAddress&=65535;
    ImGui::SameLine(); ImGui::Checkbox("Physical RAM",&physicalMemory); ImGui::SameLine(); ImGui::TextDisabled("Edit bytes while paused; press Enter to commit.");
    const float memoryHeight=std::max(100.0f,ImGui::GetContentRegionAvail().y-36);
    if (ImGui::BeginTable("Memory",18,ImGuiTableFlags_SizingFixedFit|ImGuiTableFlags_ScrollY|ImGuiTableFlags_BordersInnerV,ImVec2(0,memoryHeight))) {
        ImGui::TableSetupColumn("Address"); for (unsigned i=0;i<16;++i) { char h[4]; std::snprintf(h,sizeof(h),"%X",i); ImGui::TableSetupColumn(h); } ImGui::TableSetupColumn("Text"); ImGui::TableHeadersRow();
        for (unsigned row=0;row<16;++row) {
            const unsigned address=(memoryAddress+row*16)&65535; ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::Text("%04X",address);
            char ascii[17]={};
            for (unsigned col=0;col<16;++col) {
                const auto a=static_cast<std::uint16_t>(address+col); unsigned value=physicalMemory?m.ram()[a]:m.read(a); ascii[col]=value>=32 && value<127?static_cast<char>(value):'.';
                ImGui::TableNextColumn(); ImGui::PushID(a); ImGui::SetNextItemWidth(27); ImGui::BeginDisabled(!stop);
                if (hexInput("##byte",value,2) && value<=255) {
                    if (physicalMemory) m.ram()[a]=static_cast<std::uint8_t>(value);
                    else if (!m.write(a,static_cast<std::uint8_t>(value))) error("Mapped ROM/I/O is read-only. Select Physical RAM to edit the underlying RAM.");
                }
                remember("Debug.Memory"+std::to_string(a));
                ImGui::EndDisabled(); ImGui::PopID();
            }
            ImGui::TableNextColumn(); ImGui::TextUnformatted(ascii);
        }
        ImGui::EndTable();
    }
    ImGui::TextDisabled("Stack:"); ImGui::SameLine();
    for (unsigned n=0;n<8;++n) { const auto a=static_cast<std::uint16_t>(Z80_SP(c)+n*2); ImGui::Text("%04X",m.read(a)|(m.read(static_cast<std::uint16_t>(a+1))<<8)); if (n<7) ImGui::SameLine(); }
    ImGui::End();
}

Desktop::Desktop(SDL_Window* window,SDL_Renderer* renderer,Machine& machine,const Options& options):impl_(new Impl(window,renderer,machine,options)) {}
Desktop::~Desktop() {}
bool Desktop::running() const { return impl_->run; }
bool Desktop::paused() const { return impl_->stop; }
bool Desktop::soundEnabled() const { return impl_->audio; }
bool Desktop::capturesKeyboard() const {
    ContextScope active(impl_->context);
    return impl_->focusWindow!=SDL_GetWindowID(impl_->window) || impl_->file!=Impl::File::None || impl_->settings || impl_->about || !impl_->message.empty() || ImGui::GetIO().WantCaptureKeyboard;
}
void Desktop::pause() { impl_->stop=true; }
void Desktop::quit() { impl_->quit(); }
void Desktop::report(const std::string& error) { impl_->error(error); }
void Desktop::draw() { impl_->draw(); }
void Desktop::render(const std::string& capture) { impl_->render(capture); }
void Desktop::flushRecording() { impl_->flushRecording(); }
void Desktop::applyVirtualKeys() { for (const auto& key:impl_->virtualKeys) impl_->m.key(key.first,key.second,true); }
bool Desktop::widgetBounds(const std::string& name,float& x,float& y,float& width,float& height) const {
    const auto p=impl_->bounds.find(name); if (p==impl_->bounds.end()) return false;
    x=p->second.x; y=p->second.y; width=p->second.z; height=p->second.w; return true;
}
SDL_Window* Desktop::widgetWindow(const std::string& name) const {
    const auto p=impl_->boundWindows.find(name); return p==impl_->boundWindows.end()?NULL:SDL_GetWindowFromID(p->second);
}
void Desktop::droppedFile(const std::string& path) {
    impl_->attempt([&]() {
        const auto ext=extension(path);
        if (ext==".osn") impl_->m.loadSnapshot(path);
        else if (ext==".bin") impl_->m.loadBinary(path);
        else { impl_->flushRecording(); impl_->m.tape().load(path); impl_->tapePath=path; impl_->recordPath.clear(); }
        impl_->notice=baseName(path);
    });
}
void Desktop::event(const SDL_Event& event) {
    if (event.type==SDL_QUIT) { quit(); return; }
    Uint32 id=0;
    switch (event.type) {
    case SDL_WINDOWEVENT: id=event.window.windowID; break;
    case SDL_KEYDOWN: case SDL_KEYUP: id=event.key.windowID; break;
    case SDL_TEXTINPUT: id=event.text.windowID; break;
    case SDL_TEXTEDITING: id=event.edit.windowID; break;
    case SDL_MOUSEMOTION: id=event.motion.windowID; break;
    case SDL_MOUSEBUTTONDOWN: case SDL_MOUSEBUTTONUP: id=event.button.windowID; break;
    case SDL_MOUSEWHEEL: id=event.wheel.windowID; break;
    default: return;
    }
    Impl::Surface owner=Impl::Surface::Main; ImGuiContext* context=impl_->context;
    if (impl_->settingsWindow && impl_->settings && id==SDL_GetWindowID(impl_->settingsWindow->window)) { owner=Impl::Surface::Settings; context=impl_->settingsWindow->context; }
    else if (impl_->debuggerWindow && impl_->debugger && id==SDL_GetWindowID(impl_->debuggerWindow->window)) { owner=Impl::Surface::Debugger; context=impl_->debuggerWindow->context; }
    else if (id!=SDL_GetWindowID(impl_->window)) return;
    ContextScope active(context); ImGui_ImplSDL2_ProcessEvent(&event);
    if (event.type==SDL_WINDOWEVENT) {
        if (event.window.event==SDL_WINDOWEVENT_FOCUS_GAINED) { impl_->focusWindow=id; impl_->m.releaseKeys(); }
        if (event.window.event==SDL_WINDOWEVENT_FOCUS_LOST) { if (impl_->focusWindow==id) impl_->focusWindow=0; impl_->m.releaseKeys(); }
        if (event.window.event==SDL_WINDOWEVENT_CLOSE) {
            if (owner==Impl::Surface::Main) quit(); else impl_->closeTool(owner);
        }
        return;
    }
    if (event.type!=SDL_KEYDOWN || event.key.repeat) return;
    if (event.key.keysym.scancode==SDL_SCANCODE_ESCAPE && owner==Impl::Surface::Settings && impl_->file==Impl::File::None && impl_->message.empty() && !ImGui::GetIO().WantTextInput) {
        impl_->closeTool(owner); return;
    }
    // Editing a dialog must not resume/step/reset the paused machine through
    // a shortcut from another window (including the main toolbar).
    if (impl_->settings || impl_->file!=Impl::File::None || !impl_->message.empty()) return;
    impl_->attempt([&]() {
        switch (event.key.keysym.scancode) {
        case SDL_SCANCODE_F1: impl_->about=true; SDL_RaiseWindow(impl_->window); break;
        case SDL_SCANCODE_F2: impl_->togglePause(); break;
        case SDL_SCANCODE_F3: impl_->step(); break;
        case SDL_SCANCODE_F4: impl_->m.nmi(); break;
        case SDL_SCANCODE_F5: impl_->m.reset((event.key.keysym.mod&KMOD_SHIFT)!=0); break;
        case SDL_SCANCODE_F6: impl_->m.setTurbo(!impl_->m.turbo()); impl_->savePreferences(); break;
        case SDL_SCANCODE_F7: impl_->stepOver(); break;
        case SDL_SCANCODE_F8: if (impl_->file==Impl::File::None) impl_->open(Impl::File::SnapshotSave); break;
        case SDL_SCANCODE_F9: if (impl_->file==Impl::File::None) impl_->open(Impl::File::SnapshotOpen); break;
        case SDL_SCANCODE_F10: quit(); break;
        case SDL_SCANCODE_F11: impl_->setFullscreen(!impl_->fullscreen); impl_->savePreferences(); break;
        case SDL_SCANCODE_F12: if (impl_->file==Impl::File::None) impl_->open(Impl::File::Screenshot); break;
        case SDL_SCANCODE_HOME: if (!capturesKeyboard()) impl_->m.tape().rewind(); break;
        default: break;
        }
    });
}

}
