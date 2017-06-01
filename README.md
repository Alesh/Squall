# Squall #

The Squall is template library which provides
the event-driven and async network primitives.



### Squall general events

| Code                | Description                      |
|---------------------|----------------------------------|
| READ                | Device ready to read             |
| WRITE               | Device ready to write            |
| READ\|WRITE         | Device ready to both             |
| ERROR               | Event loop error                 |
| TIMEOUT             | Timeout of periodic call         |
| SIGNAL              | Received system signal           |
| CLEANUP             | No more event be sent            |


### Squall buffer events

| Code                | Description                      | .lastResult()| .lastError()|
|---------------------|----------------------------------|-------------:|------------:|
| BUFFER\|ERROR       | Buffer error                     |          0   |   errorno   |
| BUFFER\|WRITE       | Buffer flush to threshold        |          1   |           0 |
| BUFFER\|READ        | Buffer has ready data to read    | size to read |           0 |
| BUFFER\|ERROR       | EOF or connection reset          |          0   |           0 |
| BUFFER\|READ\|ERROR | Delimiter not found but max size |         -1   |           0 |