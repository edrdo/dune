if(LITE)
  if(RowsCoverage)
    set(TASK_ENABLED TRUE)
  else(RowsCoverage)
    set(TASK_ENABLED FALSE)
  endif(RowsCoverage)
endif(LITE)
