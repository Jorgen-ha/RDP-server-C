# Local RDP server for large file transfer
The code found in this repo is my answer to the programming exam given the spring of 2021. 

We were tasked with programming a localhost server that would transfer a large file to all connected clients, 1000 bytes at the time. The transfer of each packet had a certain chance of failing, which was to be accounted for by the server. 
Despite being a RDP server, each packet was to be acked before proceeding with delivering the next, i.e. if at any point the expected packet for the client was not the same as the expected from the server, the server would resend the last packet until it was confirmed received by the client (bearing much resemblance to TCP communication). 

The assignment dealt with many relevant aspects of server communication such as how to deal with multiple simultaneous connected clients, how to deal with package loss, and how to avoid memory loss or leaks throughout the entire process. 

See `project-description.pdf` for more details of the given assignment. 
