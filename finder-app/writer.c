#include <syslog.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

int main(int argc, char* argv[])
{
    int fd;
    ssize_t nr;

    openlog(argv[0], LOG_PID, LOG_USER);

    if ((argc != 3) || (argv[1] == NULL) || (argv[2] == NULL))
    {
        syslog(LOG_ERR,"Invalid number of argument : %d",argc);
        return 1;
    } else 
    {
        fd = open (argv[1], O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (fd == -1)
        {
            // Error
            syslog(LOG_ERR,"Can not open file \r\n");
            return 1;
        } else
        {
            /* write the string in 'buf' to 'fd' */
            nr = write (fd, argv[2], strlen (argv[2]));
            if (nr == -1) {
                // Error
                syslog(LOG_ERR,"Can not write file \r\n");
                return 1;
            } else {
                syslog(LOG_DEBUG, "Writing %s to %s Phuc Dang", argv[2], argv[1]);
                return 0;
            }            
        }
    }
    return 0;
}