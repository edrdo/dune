if(LITE)
  if(UAV.RemoteOperation)
    set(TASK_ENABLED TRUE)
  else(UAV.RemoteOperation)
    set(TASK_ENABLED FALSE)
  endif(UAV.RemoteOperation)
endif(LITE)
