if(LITE)
  if(Simulators.StreamVelocity)
    set(TASK_ENABLED TRUE)
  else(Simulators.StreamVelocity)
    set(TASK_ENABLED FALSE)
  endif(Simulators.StreamVelocity)
endif(LITE)
