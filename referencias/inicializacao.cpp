setup();                    // the normal Arduino setup() function is run here.
vTaskStartScheduler();      // initialise and run the freeRTOS scheduler. Execution should never return here.
