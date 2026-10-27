#pragma once
#include <string>
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>

// Fixed edge slots with persistent reveal buttons; resizing happens only at shared dividers.
struct DockPanels {
    struct Rect {ImVec2 pos,size;};
    float leftWidth=388,rightWidth=324,bottomHeight=90;
    bool leftHidden=false,rightHidden=false,bottomHidden=false,viewportHidden=false;
    Rect left{},right{},bottom{},viewport{};float centerX{},centerWidth{},top{},endY{};
    static constexpr float tab=28,gap=12;
    void layout(const ImGuiViewport* vp){
        float maxSide=std::max(80.0f,(vp->Size.x-268)*.5f);
        leftWidth=std::clamp(leftWidth,std::min(220.0f,maxSide),maxSide);rightWidth=std::clamp(rightWidth,std::min(230.0f,maxSide),maxSide);
        bottomHeight=std::clamp(bottomHeight,78.0f,std::max(78.0f,vp->Size.y*.45f));
        top=vp->Pos.y+42;endY=vp->Pos.y+vp->Size.y-40;
        float lw=leftHidden?tab:leftWidth,rw=rightHidden?tab:rightWidth,bh=bottomHidden?tab:bottomHeight;
        centerX=vp->Pos.x+12+lw+gap;centerWidth=std::max(64.0f,vp->Size.x-24-lw-rw-gap*2);
        left={{vp->Pos.x+12,top},{leftWidth,endY-top}};right={{vp->Pos.x+vp->Size.x-12-rw,top},{rightWidth,endY-top}};
        bottom={{centerX,endY-bh},{centerWidth,bottomHeight}};viewport={{centerX,top},{centerWidth,std::max(40.0f,endY-top-bh-8)}};
    }
    static constexpr ImGuiWindowFlags flags=ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse;
    bool begin(const char* name,Rect rect,bool& hidden,const char* hideGlyph,const char* showGlyph,ImGuiWindowFlags bodyFlags=0){
        ImGui::SetNextWindowPos(rect.pos,ImGuiCond_Always);ImGui::SetNextWindowSize(hidden?ImVec2(tab,tab):rect.size,ImGuiCond_Always);
        ImGui::SetNextWindowCollapsed(false,ImGuiCond_Always);ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize,{1,1});ImGui::Begin(name,nullptr,flags);ImGui::PopStyleVar();
        {   // colour-coded panel: a tinted title bar and accent line per panel
            std::string title=name;ImU32 accent=title.find("Surface")!=std::string::npos?IM_COL32(86,196,120,255):title.find("viewport")!=std::string::npos?IM_COL32(74,150,235,255):title.find("View")!=std::string::npos?IM_COL32(160,116,240,255):IM_COL32(240,176,76,255);
            auto* list=ImGui::GetWindowDrawList();float barWidth=hidden?tab:rect.size.x,barHeight=hidden?tab:28;
            list->AddRectFilled(rect.pos,{rect.pos.x+barWidth,rect.pos.y+barHeight},(accent&0x00ffffffu)|(70u<<24),6.0f,ImDrawFlags_RoundCornersTop);
            list->AddRectFilled(rect.pos,{rect.pos.x+barWidth,rect.pos.y+3},accent,3.0f,ImDrawFlags_RoundCornersTop);
        }
        ImGui::SetCursorScreenPos({rect.pos.x+3,rect.pos.y+3});
        if(ImGui::Button(hidden?showGlyph:hideGlyph,{22,22})){hidden=!hidden;ImGui::SetWindowSize(hidden?ImVec2(tab,tab):rect.size);}
        if(ImGui::IsItemHovered())ImGui::SetTooltip("%s %s",hidden?"Show":"Hide",name);
        if(!hidden){ImGui::SameLine();ImGui::TextUnformatted(name);ImGui::SetCursorScreenPos({rect.pos.x,rect.pos.y+30});
            ImGui::BeginChild("##dockedBody",{rect.size.x,std::max(4.0f,rect.size.y-30)},ImGuiChildFlags_AlwaysUseWindowPadding,bodyFlags);return true;
        }return false;
    }
    void end(bool body){if(body)ImGui::EndChild();ImGui::End();}
    void splitters(){
        auto divider=[&](const char* id,ImVec2 pos,ImVec2 size,float& value,bool vertical,float sign){
            ImGui::SetNextWindowPos(pos);ImGui::SetNextWindowSize(size);ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{0,0});ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize,{1,1});
            ImGui::Begin(id,nullptr,flags|ImGuiWindowFlags_NoBackground|ImGuiWindowFlags_NoNavFocus|ImGuiWindowFlags_NoNavInputs);
            ImGui::InvisibleButton("##divider",size);bool hot=ImGui::IsItemHovered()||ImGui::IsItemActive();
            if(hot)ImGui::SetMouseCursor(vertical?ImGuiMouseCursor_ResizeEW:ImGuiMouseCursor_ResizeNS);
            if(ImGui::IsItemActive()&&!ImGui::IsItemActivated())value+=sign*(vertical?ImGui::GetIO().MouseDelta.x:ImGui::GetIO().MouseDelta.y);
            auto* d=ImGui::GetWindowDrawList();d->AddRectFilled(pos,{pos.x+size.x,pos.y+size.y},hot?IM_COL32(76,174,152,220):IM_COL32(70,88,100,100),2);
            ImGui::End();ImGui::PopStyleVar(2);
        };
        if(!leftHidden)divider("##leftDockDivider",{left.pos.x+leftWidth+4,top},{4,endY-top},leftWidth,true,1);
        if(!rightHidden)divider("##rightDockDivider",{right.pos.x-8,top},{4,endY-top},rightWidth,true,-1);
        if(!bottomHidden&&!viewportHidden)divider("##bottomDockDivider",{centerX,bottom.pos.y-6},{centerWidth,4},bottomHeight,false,-1);
    }
    nlohmann::json snapshot()const{return {{"left_width",leftWidth},{"right_width",rightWidth},{"bottom_height",bottomHeight},{"left_hidden",leftHidden},{"right_hidden",rightHidden},{"bottom_hidden",bottomHidden},{"viewport_hidden",viewportHidden}};}
    void load(const nlohmann::json& j){leftWidth=j.value("left_width",388.0f);rightWidth=j.value("right_width",324.0f);bottomHeight=j.value("bottom_height",90.0f);leftHidden=j.value("left_hidden",false);rightHidden=j.value("right_hidden",false);bottomHidden=j.value("bottom_hidden",false);viewportHidden=j.value("viewport_hidden",false);if(!std::isfinite(leftWidth))leftWidth=388;if(!std::isfinite(rightWidth))rightWidth=324;if(!std::isfinite(bottomHeight))bottomHeight=90;}
};
