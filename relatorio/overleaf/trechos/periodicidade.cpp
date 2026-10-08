xTimeToWake = *pxPreviousWakeTime + xTimeIncrement;
// [...]
*pxPreviousWakeTime = xTimeToWake;
// [...]
if( xShouldDelay != pdFALSE )
{
    traceTASK_DELAY_UNTIL( xTimeToWake );

    /* prvAddCurrentTaskToDelayedList() needs the block time, not
     * the time to wake, so subtract the current tick count. */
    prvAddCurrentTaskToDelayedList( xTimeToWake - xConstTickCount, pdFALSE );
}
