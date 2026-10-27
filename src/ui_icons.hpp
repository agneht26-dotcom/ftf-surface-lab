#pragma once
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <string>
enum class ToolIcon {Navigate,Detail,Center,Settings,Spawn,Measure,Biome,LOD,Pin,Fly,Walk,Tree};
inline bool toolIconButton(const char* id,ToolIcon icon,bool selected){
 if(selected)ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.12f,.48f,.40f,1));bool clicked=ImGui::Button(id,{40,30});if(selected)ImGui::PopStyleColor();
 auto a=ImGui::GetItemRectMin();ImVec2 c{a.x+20,a.y+15};auto* d=ImGui::GetWindowDrawList();auto ink=ImGui::GetColorU32(ImGuiCol_Text);
 auto line=[&](float x,float y,float xx,float yy){d->AddLine({c.x+x,c.y+y},{c.x+xx,c.y+yy},ink,1.7f);};
 if(icon==ToolIcon::Navigate){d->AddCircle(c,7,ink,24,1.6f);line(-11,0,11,0);line(0,-11,0,11);line(-11,0,-7,-3);line(11,0,7,3);}
 if(icon==ToolIcon::Detail){d->AddCircle({c.x-2,c.y-2},6,ink,24,1.7f);line(3,3,10,10);line(-5,-2,1,-2);line(-2,-5,-2,1);}
 if(icon==ToolIcon::Center){d->AddCircle(c,7,ink,24,1.6f);d->AddCircleFilled(c,2,ink);line(-11,0,-5,0);line(5,0,11,0);line(0,-11,0,-5);line(0,5,0,11);}
 if(icon==ToolIcon::Spawn){line(-6,10,-6,-10);d->AddTriangleFilled({c.x-5,c.y-10},{c.x+9,c.y-5},{c.x-5,c.y},ink);line(-10,10,2,10);}
 if(icon==ToolIcon::Measure){d->AddRect({c.x-11,c.y-5},{c.x+11,c.y+5},ink,1,0,1.7f);for(int i=-8;i<=8;i+=4)line(float(i),-5,float(i),i%8==0?1:-1);}
 if(icon==ToolIcon::Biome){d->AddCircle({c.x-3,c.y-2},7,ink,24,1.7f);line(2,3,10,10);line(-7,0,-3,-5);line(-3,-5,1,0);line(-3,-5,-3,3);}
 if(icon==ToolIcon::Pin){d->AddCircle({c.x,c.y-4},6,ink,24,1.7f);line(-5.2f,0,0,10);line(5.2f,0,0,10);d->AddCircleFilled({c.x,c.y-4},2,ink);}
 if(icon==ToolIcon::Fly){d->AddRect({c.x-9,c.y-4},{c.x+3,c.y+6},ink,2,0,1.6f);d->AddTriangle({c.x+3,c.y-1},{c.x+10,c.y-5},{c.x+10,c.y+7},ink,1.6f);d->AddCircle({c.x-3,c.y+1},2.5f,ink,12,1.3f);line(-9,-8,-3,-8);line(-6,-11,-6,-5);}
 if(icon==ToolIcon::Walk){d->AddCircleFilled({c.x,c.y-9},3,ink);line(0,-5,0,2);line(0,-3,-6,0);line(0,-3,6,0);line(0,2,-5,10);line(0,2,5,10);}
 if(icon==ToolIcon::Tree){d->AddTriangle({c.x,c.y-11},{c.x-7,c.y-2},{c.x+7,c.y-2},ink,1.5f);d->AddTriangle({c.x,c.y-5},{c.x-9,c.y+6},{c.x+9,c.y+6},ink,1.5f);d->AddRectFilled({c.x-1.5f,c.y+6},{c.x+1.5f,c.y+11},ink);}
 if(icon==ToolIcon::LOD){for(int i=0;i<3;++i){float r=3.0f+i*3;d->AddRect({c.x-r,c.y-r},{c.x+r,c.y+r},ink,1,0,1.4f);}line(-9,0,9,0);line(0,-9,0,9);}
 if(icon==ToolIcon::Settings){d->AddCircle(c,6,ink,24,1.8f);d->AddCircle(c,2,ink,16,1.4f);for(int i=0;i<8;++i){float t=i*6.2831853f/8;line(std::cos(t)*7,std::sin(t)*7,std::cos(t)*10,std::sin(t)*10);}}
 return clicked;
}

inline int terrainFamilyId(const std::string& key){const char* names[]={"plains","steppe","dales","hills","hills","plateau","badlands","torridonian","badlands","mountains","mountains","mountains","volcano"};for(int i=0;i<13;++i)if(key==names[i])return i;return -1;}
// Icons draw at a given corner (19 x 19) so headers can place them inside their bars.
inline void terrainTypeIconAt(ImVec2 a,int family){
 auto* d=ImGui::GetWindowDrawList();auto ink=ImGui::GetColorU32(ImGuiCol_Text);auto line=[&](float x,float y,float xx,float yy){d->AddLine({a.x+x,a.y+y},{a.x+xx,a.y+yy},ink,1.6f);};
 if(family>=100){int role=family-100;
    if(role==10){for(float offset:{-3.0f,3.0f}){d->PathClear();for(int i=0;i<=16;++i){float y=float(i);d->PathLineTo({a.x+9+offset+std::sin(y*.3f)*3,a.y+1+y});}d->PathStroke(ink,0,1.5f);}return;}
    if(role==12){d->AddEllipse({a.x+9,a.y+10},{8,6},ink,0,24,1.5f);line(4,9,13,9);line(6,12,12,12);return;}
    if(role==7||role==9){line(3,12,9,role==9?1.0f:5.0f);line(9,role==9?1.0f:5.0f,15,12);line(1,16,18,16);return;}
    if(role==11){line(1,16,18,16);for(int i=4;i<=16;i+=5){line(float(i),14,float(i),3);line(float(i),9,float(i)-2,6);}return;}
    for(int i=0;i<(role==5?2:3);++i){float y=5.0f+float(i)*5;line(1,y,6,y-2);line(6,y-2,12,y+1);line(12,y+1,18,y-1);}return;}

 if(family==0||family==1){line(1,15,18,15);for(int i=4;i<18;i+=5){line(float(i),14,float(i)-2,8);line(float(i),14,float(i)+2,family==1?6.0f:10.0f);}}
 else if(family==5){line(1,16,5,6);line(5,6,14,6);line(14,6,18,16);}
 else if(family==6||family==8){line(1,16,4,4);line(4,4,8,4);line(8,4,10,12);line(10,12,13,7);line(13,7,16,7);line(16,7,18,16);line(3,12,8,12);}
 else if(family==12){line(1,16,7,7);line(7,7,12,7);line(12,7,18,16);line(8,3,7,0);line(12,3,14,0);}
 else {line(1,16,7,family>=9?2.0f:7.0f);line(7,family>=9?2.0f:7.0f,12,13);line(12,13,15,6);line(15,6,19,16);if(family>=9){line(5,6,7,8);line(7,8,9,6);}if(family==2||family==7)line(1,18,18,18);}
}
inline void terrainTypeIcon(int family){auto a=ImGui::GetCursorScreenPos();ImGui::Dummy({19,19});terrainTypeIconAt(a,family);}

inline const char* terrainTypeName(int id){const char* families[]={"Plains","Steppe","Dales","Hills","Hills 2","Plateau","Badlands","Torridonian","Badlands plateau","Mountains","Mountains","Mountains","Volcano"};const char* roles[]={"Land","Badlands","Mountain chain","Volcano","Deep ocean","Shallow ocean","Coast","Island","Beach","Island mountain","River","Wetland","Lake"};return id>=100&&id<113?roles[id-100]:id>=0&&id<13?families[id]:"None";}

// Compact vector icons stay crisp at every UI scale; stable labels preserve ImGui IDs.
inline void sectionIconAt(ImVec2 a,const char* title){
 std::string key=title;auto has=[&](const char* word){return key.find(word)!=std::string::npos;};int family=106;
 if(has("River")||has("river"))family=110;
 else if(has("Climate")||has("Biome")||has("biome"))family=111;
 else if(has("Island"))family=107;
 else if(has("Terrain")||has("terrain")||has("Height"))family=9;
 else if(has("Erosion")||has("Scree")||has("scree")||has("Surface"))family=6;
 else if(has("Underground"))family=112;
 else if(has("Walk")||has("Points")||has("tool"))family=108;
 if(has("World")||has("history")||has("version")||has("Generator")||has("Resolution")||has("View")||has("Preset")){
  ImVec2 c{a.x+9.5f,a.y+9.5f};auto* d=ImGui::GetWindowDrawList();auto ink=ImGui::GetColorU32(ImGuiCol_Text);
  if(has("Generator")||has("Resolution")||has("View")){d->AddRect({a.x+2,a.y+2},{a.x+17,a.y+17},ink,1,0,1.5f);for(int k=7;k<=12;k+=5){d->AddLine({a.x+k,a.y+2},{a.x+k,a.y+17},ink);d->AddLine({a.x+2,a.y+k},{a.x+17,a.y+k},ink);}}
  else if(has("Preset")){d->AddRect({a.x+4,a.y+2},{a.x+15,a.y+17},ink,1.5f,0,1.5f);for(float y:{7.0f,10.5f,14.0f})d->AddLine({a.x+7,a.y+y},{a.x+12,a.y+y},ink,1.2f);}
  else{d->AddCircle(c,7,ink,24,1.5f);if(has("World")){d->AddEllipse(c,{3.5f,7},ink,0,24,1.2f);d->AddLine({a.x+2,a.y+9.5f},{a.x+17,a.y+9.5f},ink,1.2f);}else{d->AddLine(c,{c.x,c.y-5},ink,1.5f);d->AddLine(c,{c.x+4,c.y+2},ink,1.5f);}}
 }else terrainTypeIconAt(a,family);
}
// Colour coding: every section header carries the colour of its category (terrain green, climate/biome teal, rivers blue,
// islands sand, erosion/surface rust, world/quality violet, walk/points rose) so long panels scan at a glance.
inline ImVec4 sectionAccent(const char* title){
 std::string key=title;auto has=[&](const char* word){return key.find(word)!=std::string::npos;};
 if(has("River")||has("river"))return {.20f,.52f,.90f,1};
 if(has("Climate")||has("Biome")||has("biome")||has("Temperature")||has("Moisture"))return {.10f,.72f,.70f,1};
 if(has("Island"))return {.86f,.70f,.32f,1};
 if(has("Erosion")||has("Scree")||has("Surface")||has("Underground")||has("Cave"))return {.88f,.46f,.22f,1};
 if(has("Terrain")||has("Height")||has("Mountain"))return {.38f,.74f,.30f,1};
 if(has("Walk")||has("Points"))return {.92f,.40f,.55f,1};
 if(has("terrain"))return {.38f,.74f,.30f,1};
 if(has("World")||has("history")||has("version")||has("Generator")||has("Resolution")||has("scene")||has("View")||has("Preset")||has("tool"))return {.62f,.45f,.95f,1};
 return {.50f,.62f,.72f,1};
}
inline bool uiSectionsStartCollapsed=false; // interactive launches start with every section closed (tests keep the old open defaults)
// One header style: a coloured bar with its icon inside (after the arrow). The title stays the ImGui ID.
inline bool sectionIconHeader(const char* title,ImGuiTreeNodeFlags flags=0){
 if(uiSectionsStartCollapsed)flags&=~ImGuiTreeNodeFlags_DefaultOpen;
 ImVec4 accent=sectionAccent(title);const auto& style=ImGui::GetStyle();ImVec2 start=ImGui::GetCursorScreenPos();
 ImGui::PushStyleColor(ImGuiCol_Header,ImVec4(accent.x*.38f,accent.y*.38f,accent.z*.38f,.95f));ImGui::PushStyleColor(ImGuiCol_HeaderHovered,ImVec4(accent.x*.55f,accent.y*.55f,accent.z*.55f,1));ImGui::PushStyleColor(ImGuiCol_HeaderActive,ImVec4(accent.x*.70f,accent.y*.70f,accent.z*.70f,1));
 std::string label=std::string("       ")+title+"###"+title;bool open=ImGui::CollapsingHeader(label.c_str(),flags);ImGui::PopStyleColor(3);
 auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();ImGui::GetWindowDrawList()->AddRectFilled({a.x,a.y},{a.x+4,b.y},ImGui::GetColorU32(accent),2);
 sectionIconAt({start.x+ImGui::GetFontSize()+style.FramePadding.x*3,(a.y+b.y)*.5f-9.5f},title);
 return open;
}
// The same bar for a section that does not fold (no arrow).
inline void sectionBar(const char* title){
 ImVec4 accent=sectionAccent(title);const auto& style=ImGui::GetStyle();auto* window=ImGui::GetCurrentWindow();
 float h=ImGui::GetFontSize()+style.FramePadding.y*2,grow=std::floor(window->WindowPadding.x*.5f-1);ImVec2 p=ImGui::GetCursorScreenPos();
 ImVec2 a{p.x-grow,p.y},b{window->WorkRect.Max.x+grow,p.y+h};auto* d=ImGui::GetWindowDrawList();
 d->AddRectFilled(a,b,ImGui::GetColorU32(ImVec4(accent.x*.38f,accent.y*.38f,accent.z*.38f,.95f)),style.FrameRounding);d->AddRectFilled(a,{a.x+4,b.y},ImGui::GetColorU32(accent),2);
 sectionIconAt({p.x+style.FramePadding.x*2,p.y+h*.5f-9.5f},title);d->AddText({p.x+style.FramePadding.x*2+25,p.y+style.FramePadding.y},ImGui::GetColorU32(ImGuiCol_Text),title);
 ImGui::Dummy({std::max(1.0f,b.x-p.x-grow),h});
}

// One value-entry style: the label above, then a slider (drag) and a number box (click to type) on one full-width row.
inline constexpr float valueBoxWidth=110;
inline void labelLine(const char* label){const char* end=ImGui::FindRenderedTextEnd(label);if(end!=label)ImGui::TextUnformatted(label,end);}
// Combos and text boxes: the label above, the control full width; returns the hidden-label ID to pass to the widget.
inline std::string labelAbove(const char* label,float width=-1){labelLine(label);ImGui::SetNextItemWidth(width);return std::string("##")+label;}
inline bool valueSlider(const char* label,float& value,float low,float high,const char* format="%.2f",ImGuiSliderFlags flags=0,bool clampTyped=true,float width=0){
 ImGui::PushID(label);ImGui::BeginGroup();labelLine(label);bool changed=false;float spacing=ImGui::GetStyle().ItemSpacing.x,row=width>0?width:ImGui::GetContentRegionAvail().x;
 ImGui::SetNextItemWidth(std::max(40.0f,row-valueBoxWidth-spacing));float slider=value;if(ImGui::SliderFloat("##slider",&slider,low,high,"",flags|ImGuiSliderFlags_NoInput)){value=slider;changed=true;}
 ImGui::SameLine();ImGui::SetNextItemWidth(valueBoxWidth);float typed=value;if(ImGui::InputFloat("##number",&typed,0,0,format)&&std::isfinite(typed)){value=clampTyped?std::clamp(typed,std::min(low,high),std::max(low,high)):typed;changed=true;}
 ImGui::EndGroup();ImGui::PopID();return changed;
}
inline bool valueSliderInt(const char* label,int& value,int low,int high,const char* format="%d",ImGuiSliderFlags flags=0,float width=0){
 ImGui::PushID(label);ImGui::BeginGroup();labelLine(label);bool changed=false;float spacing=ImGui::GetStyle().ItemSpacing.x,row=width>0?width:ImGui::GetContentRegionAvail().x;
 ImGui::SetNextItemWidth(std::max(40.0f,row-valueBoxWidth-spacing));int slider=value;if(ImGui::SliderInt("##slider",&slider,low,high,"",flags|ImGuiSliderFlags_NoInput)){value=slider;changed=true;}
 ImGui::SameLine();ImGui::SetNextItemWidth(valueBoxWidth);int typed=value;if(ImGui::InputScalar("##number",ImGuiDataType_S32,&typed,nullptr,nullptr,format)){value=std::clamp(typed,low,high);changed=true;}
 ImGui::EndGroup();ImGui::PopID();return changed;
}
