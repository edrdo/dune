if(LITE)
  if(VehicleFormation.FormCollAvoid)
    set(TASK_ENABLED TRUE)
  else(TVehicleFormation.FormCollAvoid)
    set(TASK_ENABLED FALSE)
  endif(VehicleFormation.FormCollAvoid)
else(LITE)
  set(TASK_ENABLED FALSE)
endif(LITE)
