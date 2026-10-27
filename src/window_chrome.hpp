#pragma once
#include <windows.h>
#include <windowsx.h>
#include <GLFW/glfw3.h>

// A single client-side caption, with Win32 edge resizing, dragging and work-area
// maximization. GLFW still handles WM_CLOSE, so the save prompt remains intact.
class WindowChrome {
public:
    GLFWwindow* window;HWND hwnd;WNDPROC previous{};RECT drag{48,0,1200,28};
    explicit WindowChrome(GLFWwindow* w,HWND h):window(w),hwnd(h){SetPropW(hwnd,L"FTF.Chrome",this);previous=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(hwnd,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(proc)));refresh();}
    void refresh(){if(glfwGetWindowMonitor(window))return;auto style=GetWindowLongPtrW(hwnd,GWL_STYLE);style&=~LONG_PTR(WS_CAPTION);style|=WS_THICKFRAME|WS_MAXIMIZEBOX|WS_MINIMIZEBOX|WS_SYSMENU;SetWindowLongPtrW(hwnd,GWL_STYLE,style);SetWindowPos(hwnd,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);}
    void caption(float left,float right,float height){drag={LONG(left),0,LONG(right),LONG(height)};}
    static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){
        auto* self=static_cast<WindowChrome*>(GetPropW(h,L"FTF.Chrome"));if(!self)return DefWindowProcW(h,m,w,l);
        if(m==WM_NCCALCSIZE&&w)return 0;
        if(m==WM_GETMINMAXINFO&&!glfwGetWindowMonitor(self->window)){
            auto result=CallWindowProcW(self->previous,h,m,w,l);MONITORINFO info{sizeof(info)};
            if(GetMonitorInfoW(MonitorFromWindow(h,MONITOR_DEFAULTTONEAREST),&info)){auto* mm=reinterpret_cast<MINMAXINFO*>(l);mm->ptMaxPosition={info.rcWork.left-info.rcMonitor.left,info.rcWork.top-info.rcMonitor.top};mm->ptMaxSize={info.rcWork.right-info.rcWork.left,info.rcWork.bottom-info.rcWork.top};}return result;
        }
        if(m==WM_NCHITTEST&&!glfwGetWindowMonitor(self->window)){
            POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};ScreenToClient(h,&p);RECT r;GetClientRect(h,&r);
            if(!IsZoomed(h)){int edge=std::max(5,int(GetDpiForWindow(h)*6/96));bool left=p.x<edge,right=p.x>=r.right-edge,top=p.y<edge,bottom=p.y>=r.bottom-edge;
                if(top&&left)return HTTOPLEFT;if(top&&right)return HTTOPRIGHT;if(bottom&&left)return HTBOTTOMLEFT;if(bottom&&right)return HTBOTTOMRIGHT;if(left)return HTLEFT;if(right)return HTRIGHT;if(top)return HTTOP;if(bottom)return HTBOTTOM;
            }
            if(PtInRect(&self->drag,p))return HTCAPTION;
            return HTCLIENT;
        }
        return CallWindowProcW(self->previous,h,m,w,l);
    }
};
