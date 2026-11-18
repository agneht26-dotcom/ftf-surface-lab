float mcDivF(float a,float b){return float(double(a)/double(b));}
const ivec3 MC_SPLINE_NODES[]=ivec3[](ivec3(2,0,2),ivec3(2,2,2),ivec3(2,4,2),ivec3(2,6,2),ivec3(3,8,2),ivec3(1,10,10),ivec3(2,20,2),ivec3(2,22,2),ivec3(3,24,2),ivec3(1,26,10),ivec3(2,36,2),ivec3(2,38,2),ivec3(3,40,2),ivec3(1,42,10),ivec3(2,52,2),ivec3(3,54,2),ivec3(3,56,2),ivec3(1,58,11),ivec3(0,69,5));
const float MC_SPLINE_LOC[]=float[](-0.20000000298023224,0.20000000298023224,-0.05000000074505806,0.05000000074505806,-0.05000000074505806,0.05000000074505806,0.0,0.10000000149011612,-0.8999999761581421,-0.6899999976158142,-0.6000000238418579,-0.5,-0.3499999940395355,-0.25,-0.10000000149011612,0.029999999329447746,0.3499999940395355,0.44999998807907104,0.550000011920929,0.6200000047683716,-0.20000000298023224,0.20000000298023224,0.0,0.10000000149011612,-0.8999999761581421,-0.6899999976158142,-0.6000000238418579,-0.5,-0.3499999940395355,-0.25,-0.10000000149011612,0.029999999329447746,0.3499999940395355,0.44999998807907104,0.550000011920929,0.6200000047683716,-0.20000000298023224,0.20000000298023224,0.0,0.10000000149011612,-0.8999999761581421,-0.6899999976158142,-0.6000000238418579,-0.5,-0.3499999940395355,-0.25,-0.10000000149011612,0.029999999329447746,0.3499999940395355,0.44999998807907104,0.550000011920929,0.6200000047683716,-0.20000000298023224,0.20000000298023224,0.44999998807907104,0.699999988079071,-0.699999988079071,-0.15000000596046448,-0.6000000238418579,-0.5,-0.3499999940395355,-0.25,-0.10000000149011612,0.029999999329447746,0.05000000074505806,0.4000000059604645,0.44999998807907104,0.550000011920929,0.5799999833106995,-0.1899999976158142,-0.15000000596046448,-0.10000000149011612,0.029999999329447746,0.05999999865889549);
const float MC_SPLINE_DERIV[]=float[](0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0);
const int MC_SPLINE_CHILD[]=int[](-1,-1,-1,-1,-1,-1,-1,-1,-1,3,0,1,0,0,2,0,-1,4,4,-1,-1,-1,-1,-1,-1,7,6,1,6,6,2,6,-1,8,8,-1,-1,-1,-1,-1,-1,11,10,1,10,10,2,10,-1,12,12,-1,-1,-1,14,-1,14,-1,14,1,14,14,2,14,15,15,16,16,-1,-1,5,9,13,17);
const float MC_SPLINE_LEAF[]=float[](6.300000190734863,6.25,6.300000190734863,2.6700000762939453,2.6700000762939453,6.300000190734863,6.25,0.625,6.25,0.0,0.0,0.0,0.0,0.0,0.0,0.0,6.25,0.0,0.0,6.25,6.300000190734863,5.46999979019165,5.46999979019165,0.625,5.46999979019165,0.0,0.0,0.0,0.0,0.0,0.0,0.0,5.46999979019165,0.0,0.0,5.46999979019165,6.300000190734863,5.079999923706055,5.079999923706055,0.625,5.079999923706055,0.0,0.0,0.0,0.0,0.0,0.0,0.0,5.079999923706055,0.0,0.0,5.079999923706055,6.300000190734863,4.690000057220459,0.0,1.559999942779541,0.0,1.3700000047683716,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,4.690000057220459,3.950000047683716,0.0,0.0,0.0,0.0);
float mcFactorSplineEval(vec4 c,int root){
 int node[6];int phase[6];int ptIdx[6];float xval[6];float aval[6];int sp=0;node[0]=root;phase[0]=0;float ret=0.0;
 while(sp>=0){
  int ph=phase[sp];
  if(ph==0){
   ivec3 nd=MC_SPLINE_NODES[node[sp]];float x=c[nd.x];int i=-1;
   for(int j=0;j<nd.z;++j)if(!(x<MC_SPLINE_LOC[nd.y+j]))i=j;
   if(i<0||i==nd.z-1){
    int p=nd.y+(i<0?0:nd.z-1);int ch=MC_SPLINE_CHILD[p];
    if(ch<0){float v=MC_SPLINE_LEAF[p],d=MC_SPLINE_DERIV[p];if(d==0.0){ret=v;}else{precise float r=v+d*(x-MC_SPLINE_LOC[p]);ret=r;}--sp;}
    else{ptIdx[sp]=p;xval[sp]=x;phase[sp]=3;++sp;node[sp]=ch;phase[sp]=0;}
   }else{
    int p=nd.y+i;ptIdx[sp]=p;xval[sp]=x;int chA=MC_SPLINE_CHILD[p];
    if(chA<0){
     aval[sp]=MC_SPLINE_LEAF[p];int chB=MC_SPLINE_CHILD[p+1];
     if(chB<0){float a=aval[sp],b=MC_SPLINE_LEAF[p+1],l0=MC_SPLINE_LOC[p],l1=MC_SPLINE_LOC[p+1],d0=MC_SPLINE_DERIV[p],d1=MC_SPLINE_DERIV[p+1];precise float span=l1-l0,t=mcDivF(x-l0,span),diff=b-a,p0=d0*span-diff,q0=-d1*span+diff;precise float r=(a+t*(b-a))+t*(1.0-t)*(p0+t*(q0-p0));ret=r;--sp;}
     else{phase[sp]=2;++sp;node[sp]=chB;phase[sp]=0;}
    }else{phase[sp]=1;++sp;node[sp]=chA;phase[sp]=0;}
   }
  }else if(ph==1){
   aval[sp]=ret;int p=ptIdx[sp];int chB=MC_SPLINE_CHILD[p+1];
   if(chB<0){float a=aval[sp],b=MC_SPLINE_LEAF[p+1],l0=MC_SPLINE_LOC[p],l1=MC_SPLINE_LOC[p+1],d0=MC_SPLINE_DERIV[p],d1=MC_SPLINE_DERIV[p+1];precise float span=l1-l0,t=mcDivF(xval[sp]-l0,span),diff=b-a,p0=d0*span-diff,q0=-d1*span+diff;precise float r=(a+t*(b-a))+t*(1.0-t)*(p0+t*(q0-p0));ret=r;--sp;}
   else{phase[sp]=2;++sp;node[sp]=chB;phase[sp]=0;}
  }else if(ph==2){
   int p=ptIdx[sp];float a=aval[sp],b=ret,l0=MC_SPLINE_LOC[p],l1=MC_SPLINE_LOC[p+1],d0=MC_SPLINE_DERIV[p],d1=MC_SPLINE_DERIV[p+1];precise float span=l1-l0,t=mcDivF(xval[sp]-l0,span),diff=b-a,p0=d0*span-diff,q0=-d1*span+diff;precise float r=(a+t*(b-a))+t*(1.0-t)*(p0+t*(q0-p0));ret=r;--sp;
  }else{
   int p=ptIdx[sp];float v=ret,d=MC_SPLINE_DERIV[p];if(d==0.0){ret=v;}else{precise float r=v+d*(xval[sp]-MC_SPLINE_LOC[p]);ret=r;}--sp;
  }
 }
 return ret;
}
double mcFactor(float continents,float erosion,float ridges){precise double folded=-0.6666666666666666LF+abs(double(ridges));folded=-0.3333333333333333LF+abs(folded);folded=-3.0LF*folded;precise double v=-10.0LF+double(mcFactorSplineEval(vec4(continents,erosion,ridges,float(folded)),18));v=1.0LF*v;v=10.0LF+v;return v;}
