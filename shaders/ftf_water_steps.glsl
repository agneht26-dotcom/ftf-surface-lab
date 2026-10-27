// Exact original-JAR hydrology table (Random seed42), static balanced decisions.
// No dynamically indexed private double arrays in the GPU kernel.
float ftfSteppedWater(float gradient){
    double value=clamp(double(gradient),0.0LF,1.0LF);
    if(value<=0.5435980578088206LF){
        if(value<=0.2640269446059727LF){
            if(value<=0.13207675363741375LF){
                if(value<=0.07962184773132452LF){
                    if(value<0.05962184773132452LF){
                        return float(0.0LF);
                    }else{
                        precise double t=(value-0.05962184773132452LF)/.02LF;
                        precise double eased=t*t*(3.0LF-2.0LF*t);
                        return float(0.0LF+eased*(0.07364909551787992LF-0.0LF));
                    }
                }else{
                    if(value<0.11207675363741375LF){
                        return float(0.07364909551787992LF);
                    }else{
                        precise double t=(value-0.11207675363741375LF)/.02LF;
                        precise double eased=t*t*(3.0LF-2.0LF*t);
                        return float(0.07364909551787992LF+eased*(0.12201791958729966LF-0.07364909551787992LF));
                    }
                }
            }else{
                if(value<=0.2076762216509246LF){
                    if(value<0.1876762216509246LF){
                        return float(0.12201791958729966LF);
                    }else{
                        precise double t=(value-0.1876762216509246LF)/.02LF;
                        precise double eased=t*t*(3.0LF-2.0LF*t);
                        return float(0.12201791958729966LF+eased*(0.20937005587839133LF-0.12201791958729966LF));
                    }
                }else{
                    if(value<0.2440269446059727LF){
                        return float(0.20937005587839133LF);
                    }else{
                        precise double t=(value-0.2440269446059727LF)/.02LF;
                        precise double eased=t*t*(3.0LF-2.0LF*t);
                        return float(0.20937005587839133LF+eased*(0.25765606864582485LF-0.20937005587839133LF));
                    }
                }
            }
        }else{
            if(value<=0.41859112891138806LF){
                if(value<=0.3265311385038961LF){
                    if(value<0.3065311385038961LF){
                        return float(0.25765606864582485LF);
                    }else{
                        precise double t=(value-0.3065311385038961LF)/.02LF;
                        precise double eased=t*t*(3.0LF-2.0LF*t);
                        return float(0.25765606864582485LF+eased*(0.33750958567049605LF-0.25765606864582485LF));
                    }
                }else{
                    if(value<0.39859112891138804LF){
                        return float(0.33750958567049605LF);
                    }else{
                        precise double t=(value-0.39859112891138804LF)/.02LF;
                        precise double eased=t*t*(3.0LF-2.0LF*t);
                        return float(0.33750958567049605LF+eased*(0.3958009523253084LF-0.33750958567049605LF));
                    }
                }
            }else{
                if(value<=0.4996621489009155LF){
                    if(value<0.47966214890091546LF){
                        return float(0.3958009523253084LF);
                    }else{
                        precise double t=(value-0.47966214890091546LF)/.02LF;
                        precise double eased=t*t*(3.0LF-2.0LF*t);
                        return float(0.3958009523253084LF+eased*(0.4509848207754202LF-0.3958009523253084LF));
                    }
                }else{
                    if(value<0.5235980578088206LF){
                        return float(0.4509848207754202LF);
                    }else{
                        precise double t=(value-0.5235980578088206LF)/.02LF;
                        precise double eased=t*t*(3.0LF-2.0LF*t);
                        return float(0.4509848207754202LF+eased*(0.5191020302804192LF-0.4509848207754202LF));
                    }
                }
            }
        }
    }else{
        if(value<=0.7844483528244987LF){
            if(value<=0.6332359652832487LF){
                if(value<=0.5896347777054164LF){
                    if(value<0.5696347777054164LF){
                        return float(0.5191020302804192LF);
                    }else{
                        precise double t=(value-0.5696347777054164LF)/.02LF;
                        precise double eased=t*t*(3.0LF-2.0LF*t);
                        return float(0.5191020302804192LF+eased*(0.6016360481417011LF-0.5191020302804192LF));
                    }
                }else{
                    if(value<0.6132359652832486LF){
                        return float(0.6016360481417011LF);
                    }else{
                        precise double t=(value-0.6132359652832486LF)/.02LF;
                        precise double eased=t*t*(3.0LF-2.0LF*t);
                        return float(0.6016360481417011LF+eased*(0.669322368622497LF-0.6016360481417011LF));
                    }
                }
            }else{
                if(value<=0.7143961198539354LF){
                    if(value<0.6943961198539353LF){
                        return float(0.669322368622497LF);
                    }else{
                        precise double t=(value-0.6943961198539353LF)/.02LF;
                        precise double eased=t*t*(3.0LF-2.0LF*t);
                        return float(0.669322368622497LF+eased*(0.7359886872060758LF-0.669322368622497LF));
                    }
                }else{
                    if(value<0.7644483528244986LF){
                        return float(0.7359886872060758LF);
                    }else{
                        precise double t=(value-0.7644483528244986LF)/.02LF;
                        precise double eased=t*t*(3.0LF-2.0LF*t);
                        return float(0.7359886872060758LF+eased*(0.8139504814423302LF-0.7359886872060758LF));
                    }
                }
            }
        }else{
            if(value<=0.9043915631493255LF){
                if(value<=0.8189170348014415LF){
                    if(value<0.7989170348014415LF){
                        return float(0.8139504814423302LF);
                    }else{
                        precise double t=(value-0.7989170348014415LF)/.02LF;
                        precise double eased=t*t*(3.0LF-2.0LF*t);
                        return float(0.8139504814423302LF+eased*(0.8673512359103648LF-0.8139504814423302LF));
                    }
                }else{
                    if(value<0.8843915631493254LF){
                        return float(0.8673512359103648LF);
                    }else{
                        precise double t=(value-0.8843915631493254LF)/.02LF;
                        precise double eased=t*t*(3.0LF-2.0LF*t);
                        return float(0.8673512359103648LF+eased*(0.9244721935312025LF-0.8673512359103648LF));
                    }
                }
            }else{
                if(value<=1.0LF){
                    if(value<0.98LF){
                        return float(0.9244721935312025LF);
                    }else{
                        precise double t=(value-0.98LF)/.02LF;
                        precise double eased=t*t*(3.0LF-2.0LF*t);
                        return float(0.9244721935312025LF+eased*(1.0LF-0.9244721935312025LF));
                    }
                }else{
                    return float(1.0LF);
                }
            }
        }
    }
}
int ftfWaterStepId(float gradient){double value=clamp(double(gradient),0.0LF,1.0LF);
if(value<0.05962184773132452LF)return -1;
if(value<0.07962184773132452LF)return -2;
if(value<0.11207675363741375LF)return 0;
if(value<0.13207675363741375LF)return -2;
if(value<0.1876762216509246LF)return 1;
if(value<0.2076762216509246LF)return -2;
if(value<0.2440269446059727LF)return 2;
if(value<0.2640269446059727LF)return -2;
if(value<0.3065311385038961LF)return 3;
if(value<0.3265311385038961LF)return -2;
if(value<0.39859112891138804LF)return 4;
if(value<0.41859112891138806LF)return -2;
if(value<0.47966214890091546LF)return 5;
if(value<0.4996621489009155LF)return -2;
if(value<0.5235980578088206LF)return 6;
if(value<0.5435980578088206LF)return -2;
if(value<0.5696347777054164LF)return 7;
if(value<0.5896347777054164LF)return -2;
if(value<0.6132359652832486LF)return 8;
if(value<0.6332359652832487LF)return -2;
if(value<0.6943961198539353LF)return 9;
if(value<0.7143961198539354LF)return -2;
if(value<0.7644483528244986LF)return 10;
if(value<0.7844483528244987LF)return -2;
if(value<0.7989170348014415LF)return 11;
if(value<0.8189170348014415LF)return -2;
if(value<0.8843915631493254LF)return 12;
if(value<0.9043915631493255LF)return -2;
if(value<0.98LF)return 13;
if(value<1.0LF)return -2;
return 14;}
float ftfWaterFlatness(float gradient){double value=clamp(double(gradient),0.0LF,1.0LF);
if(value<0.05962184773132452LF){double mid=0.02981092386566226LF,radius=mid;double t=1.0LF-abs(value-mid)/radius;return float(t*t*(3.0LF-2.0LF*t));}
if(value>=0.07962184773132452LF&&value<=0.11207675363741375LF){double t=1.0LF-abs(value-0.09584930068436914LF)/0.016227452953044612LF;return float(t*t*(3.0LF-2.0LF*t));}
if(value>=0.13207675363741375LF&&value<=0.1876762216509246LF){double t=1.0LF-abs(value-0.15987648764416917LF)/0.027799734006755422LF;return float(t*t*(3.0LF-2.0LF*t));}
if(value>=0.2076762216509246LF&&value<=0.2440269446059727LF){double t=1.0LF-abs(value-0.22585158312844866LF)/0.018175361477524063LF;return float(t*t*(3.0LF-2.0LF*t));}
if(value>=0.2640269446059727LF&&value<=0.3065311385038961LF){double t=1.0LF-abs(value-0.2852790415549344LF)/0.021252096948961685LF;return float(t*t*(3.0LF-2.0LF*t));}
if(value>=0.3265311385038961LF&&value<=0.39859112891138804LF){double t=1.0LF-abs(value-0.3625611337076421LF)/0.036029995203745974LF;return float(t*t*(3.0LF-2.0LF*t));}
if(value>=0.41859112891138806LF&&value<=0.47966214890091546LF){double t=1.0LF-abs(value-0.4491266389061518LF)/0.030535509994763704LF;return float(t*t*(3.0LF-2.0LF*t));}
if(value>=0.4996621489009155LF&&value<=0.5235980578088206LF){double t=1.0LF-abs(value-0.511630103354868LF)/0.011967954453952573LF;return float(t*t*(3.0LF-2.0LF*t));}
if(value>=0.5435980578088206LF&&value<=0.5696347777054164LF){double t=1.0LF-abs(value-0.5566164177571185LF)/0.013018359948297864LF;return float(t*t*(3.0LF-2.0LF*t));}
if(value>=0.5896347777054164LF&&value<=0.6132359652832486LF){double t=1.0LF-abs(value-0.6014353714943326LF)/0.011800593788916125LF;return float(t*t*(3.0LF-2.0LF*t));}
if(value>=0.6332359652832487LF&&value<=0.6943961198539353LF){double t=1.0LF-abs(value-0.663816042568592LF)/0.030580077285343343LF;return float(t*t*(3.0LF-2.0LF*t));}
if(value>=0.7143961198539354LF&&value<=0.7644483528244986LF){double t=1.0LF-abs(value-0.739422236339217LF)/0.025026116485281635LF;return float(t*t*(3.0LF-2.0LF*t));}
if(value>=0.7844483528244987LF&&value<=0.7989170348014415LF){double t=1.0LF-abs(value-0.7916826938129701LF)/0.007234340988471422LF;return float(t*t*(3.0LF-2.0LF*t));}
if(value>=0.8189170348014415LF&&value<=0.8843915631493254LF){double t=1.0LF-abs(value-0.8516542989753835LF)/0.03273726417394196LF;return float(t*t*(3.0LF-2.0LF*t));}
if(value>=0.9043915631493255LF&&value<=0.98LF){double t=1.0LF-abs(value-0.9421957815746627LF)/0.037804218425337266LF;return float(t*t*(3.0LF-2.0LF*t));}
if(value>=1.0LF&&value<=1.0LF){return 0.0;}
return 0.0;}
