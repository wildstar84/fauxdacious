/*
 * art.c
 * Copyright 2011-2012 John Lindgren
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the documentation
 *    provided with the distribution.
 *
 * This software is provided "as is" and without any warranty, express or
 * implied. In no event shall the authors be liable for any damages arising from
 * the use of this software.
 */

#include "probe.h"
#include "internal.h"

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <glib/gstdio.h>

#include "audstrings.h"
#include "hook.h"
#include "mainloop.h"
#include "multihash.h"
#include "runtime.h"
#include "scanner.h"
#include "vfs.h"

#define FLAG_DONE 1
#define FLAG_SENT 2

struct AudArtItem {
    String filename;
    int refcount;
    int flag;

    /* album art as JPEG or PNG data */
    Index<char> data;

    /* album art as (possibly a temporary) file */
    String art_file;
    bool is_temp;
};

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

static SimpleHash<String, AudArtItem> art_items;
static AudArtItem * current_item;
static QueuedFunc queued_requests;

static Index<AudArtItem *> get_queued ()
{
    Index<AudArtItem *> queued;
    pthread_mutex_lock (& mutex);

    art_items.iterate ([&] (const String &, AudArtItem & item)
    {
        if (item.flag == FLAG_DONE)
        {
            queued.append (& item);
            item.flag = FLAG_SENT;
        }
    });

    queued_requests.stop ();

    pthread_mutex_unlock (& mutex);
    return queued;
}

static void send_requests ()
{
    auto queued = get_queued ();
    for (AudArtItem * item : queued)
    {
        hook_call ("art ready", (void *) (const char *) item->filename);
        aud_art_unref (item); /* release temporary reference */
    }
}

static void finish_item_locked (AudArtItem * item, Index<char> && data, String && art_file)
{
    /* already finished? */
    if (item->flag)
        return;

    item->data = std::move (data);
    item->art_file = std::move (art_file);
    item->flag = FLAG_DONE;

    queued_requests.queue (send_requests);
}

static void request_callback (ScanRequest * request)
{
    pthread_mutex_lock (& mutex);

    AudArtItem * item = art_items.lookup (request->filename);

    if (item)
        finish_item_locked (item, std::move (request->image_data), std::move (request->image_file));

    pthread_mutex_unlock (& mutex);
}

static void art_item_unref_locked (AudArtItem * item)
{
    if (! -- item->refcount)
    {
        /* delete temporary file */
        if (item->art_file && item->is_temp)
        {
            StringBuf local = uri_to_filename (item->art_file);
            if (local)
                g_unlink (local);
        }
        art_items.remove (item->filename);
    }
}

static void clear_current_locked ()
{
    if (current_item)
    {
        art_item_unref_locked (current_item);
        current_item = nullptr;
    }
}

/* JWT:CHECK THE USER TAG FILES, AND FOR WEB URLS: CHECK FOR ART FILE MATCHING BASE URL: */

static int check_tag_file (const String & filename, AudArtItem * item, const char * tagfile, int prevprec)
{
    /* JWT:LOOK FOR IMAGE IN TAG DATA FILE(S) UNDER "Comment": */
    Tuple img_tuple = Tuple ();
    int precedence = aud_read_tag_from_tagfile (filename, tagfile, img_tuple);

    if (precedence <= prevprec)
        return prevprec;  // SKIP THIS TAG FILE, WE ALREADY HAVE IMG. FROM ONE W/SAME OR HIGHER PRECEDENCE!
    else
    {
        /* SEARCH TAG FILE FOR ART: */
        String tfld = img_tuple.get_str (Tuple::Comment);
        const char * tfld_charptr = (const char *) tfld;
        if (tfld_charptr && ! strncmp (tfld_charptr, "file://", 7))  // TAG FILE COMMENT CONTAINS A FILE (ASSUME IMG!)!
        {
            const char * sep = strstr (tfld_charptr, ";");  // IF "file1;file2" ONLY EXTRACT "file1"!:
            String art_file = sep ? String (str_printf ("%.*s", (int)(sep - tfld_charptr), tfld_charptr)) : tfld;
            if (art_file && art_file[0])
            {
                bool fileIsImage = false;
                Index<String> extlist = str_list_to_index ("jpg,png,jpeg,gif,com,webp", ",");
                String ext = String (uri_get_extension (art_file));
                for (auto & tryext : extlist)
                {
                    if (strcmp_nocase (ext, tryext))
                        continue;

                    fileIsImage = true;
                    break;
                }
                if (fileIsImage)  // WE'RE A VALID IMAGE FILE (BY EXTENSION)!:
                {
                    VFSFile file (art_file, "r");
                    if (file)
                    {
                        AUDINFO ("Art img. from %s, prec=%d file=%s.\n", tagfile, precedence, (const char *) art_file);
                        item->data = file.read_all ();
                        if (item->data.len () > 0)
                        {
                            item->art_file = art_file;
                            /* JWT:(ALBUMART PLUGINS): DON'T SEARCH WEB SINCE WE HAVE ART IMAGE ALREADY -
                               THIS ADDED B/C PODCASTS MAY SEND US A BAD IMAGE THAT BLANKS OUR GOOD ONE!
                            */
                            if (! strcmp (tagfile, "tmp_tag_data"))
	                            aud_set_bool (nullptr, "_skip_web_art_search", true);
                            return precedence;
                        }
                    }
                }
            }
        }
    }

    return prevprec;
}

/* JWT:THIS FUNCTION NOW SUPPORTS DIRECT REQUESTS FOR SPECIFIC IMAGE FILES (filename IS A JPG|PNG, ETC.):
   THIS IS USED DIRECTLY BY infowin.cc AND INDIRECTLY BY THE ALBUMART PLUGIN:
*/
static int check_for_user_art (const String & filename, AudArtItem * item, bool itemExists)
{
    bool foundArt = false;     // TRUE IF HAVE EXISTING ART DATA.
    int forcetagcheck = false; // TRUE TO FORCE FULL TAG-FILE CHECK, OTHERWISE, ONLY CHECK FOR "OVERRIDE" OR "ONLY".

    if (! itemExists || item->data.len () <= 0)  // NEW ITEM, SEE IF REQUESTED FILE IS ALREADY AN IMAGE FILE:
    {
        bool fileIsImage = false;
        String ext = String (uri_get_extension (filename));
        Index<String> extlist = str_list_to_index ("jpg,png,jpeg,gif,com,webp", ",");
        for (auto & tryext : extlist)
        {
            if (strcmp_nocase (ext, tryext))
                continue;

            fileIsImage = true;
            break;
        }
        if (fileIsImage)  // infowin.cc:audgui_pixbuf_request () CAN NOW REQUEST A SPECIFIC IMAGE FILE!:
        {
            struct stat statbuf;
            StringBuf coverart_file = uri_to_filename (filename);
            if (stat ((const char *) coverart_file, &statbuf) >= 0)  // ART IMAGE FILE EXISTS, FETCH IT AS-IS!:
            {
                VFSFile file (filename, "r");
                if (file)
                {
                    item->data = file.read_all ();
                    if (item->data.len () > 0)
                    {
                        item->art_file = filename;
                        return 1;  // FILE IS AN IMAGE, SO WE'RE DONE (FLAG_SENT - DON'T BOTHER SENDING "art ready")!
                    }
                }
            }
            return -1;  // FILE IS AN IMAGE (NOT A SONG ENTRY), BUT COULDN'T READ? SO WE'RE DONE (DON'T SCAN)!
        }
    }
    else  // WE HAVE IMAGE DATA ALREADY:
    {
        foundArt = true;
        if (aud_get_bool (nullptr, "no_dynamic_stream_metadata")
                && (! strncmp (filename, "http://", 7) || ! strncmp (filename, "https://", 8)))
            forcetagcheck = true;  // FOR STREAMS, FORCE FULL TAG-FILE CHECK IF WE PROHIBIT STREAMS TO SET WHEN PLAYING!
    }

    aud_set_bool (nullptr, "_skip_web_art_search", false);
    if (! item->art_file && aud_get_bool (nullptr, "user_tag_data"))  // ONLY CHECK TAG-FILES IF USER WANTS TO USE THEM:
    {
        /* NORMALLY ONLY CHECK TAG-FILES "OVERRIDE"|"ONLY" IF HAVE EXISTING ART FILE, UNLESS FULL CHECK FORCED! */
        int minprec = (! foundArt || forcetagcheck) ? 0 : 1;  // CONSIDER TAG-FILE ONLY IF PRECEDENCE EXCEEDS.
        int prevprec = check_tag_file (filename, item, "tmp_tag_data", minprec);  // 1ST, CHECK TEMP TAGFILE
        if (! strncmp (filename, "file://", 7))  // 2ND, CHECK DIRECTORY TAGFILE (IF ENTRY IS FILE)
        {
            StringBuf tag_fid = uri_to_filename (filename);
            StringBuf path = filename_get_parent (tag_fid);
            String filename_only = String (filename_get_base (tag_fid));
            prevprec = check_tag_file (filename_only, item,
                    filename_to_uri (str_concat ({path, "/", filename_only, ".tag"})), prevprec);
            prevprec = check_tag_file (filename_only, item,
                    filename_to_uri (str_concat ({path, "/user_tag_data.tag"})), prevprec);
        }
        prevprec = check_tag_file (filename, item, "user_tag_data", prevprec);  // 3RD, CHECK USER TAGFILE
        if (! foundArt)
            foundArt = prevprec > minprec;  // SET TRUE ONLY IF MATCHED TAG-FILE W/SUFFICIENT PRECEDENCE!
    }
    if (! foundArt && item->data.len () <= 0 && ! item->art_file)  // 4TH, CHECK BASE URL-NAMED FILE (IF WEB URL):
    {
        StringBuf scheme = uri_get_scheme (filename);
        if (strcmp_safe (scheme, "file") && strcmp_safe (scheme, "stdin")
                && strcmp_safe (scheme, "cdda") && strcmp_safe (scheme, "dvd"))
        {
            /* JWT:URL & NO OTHER ART FOUND, SO LOOK FOR IMAGE MATCHING STREAMING URL (IE: www.streamingradio.com.jpg): */
            const char * slash = strstr (filename, "//");
            if (slash)
            {
                slash+=2;
                const char * endbase = strstr (slash, "/");
                int ln = endbase ? endbase - slash : -1;
                String urlbase = String (str_copy (slash, ln));
                auto split = str_list_to_index (slash, "?&#:/");
                for (auto & str : split)
                {
                    urlbase = String (str_copy (str));
                    break;
                }
                Index<String> extlist = str_list_to_index ("jpg,png,jpeg,gif,com,webp", ",");
                struct stat statbuf;
                for (auto & ext : extlist)
                {
                    String coverart_file = String (str_concat ({aud_get_path (AudPath::UserDir), "/",
                            urlbase, ".", (const char *) ext}));
                    if (stat ((const char *) coverart_file, &statbuf) >= 0)  // ART IMAGE FILE DOESN'T EXIST:
                    {
                        coverart_file = String (filename_to_uri (coverart_file));
                        item->art_file = coverart_file;
                        VFSFile file (item->art_file, "r");
                        if (file)
                        {
                            item->data = file.read_all ();
                            if (item->data.len () > 0)
                            {
                                foundArt = true;
                                break;
                            }
                        }
                    }
                }
            }
        }
    }

    return foundArt ? 1 : 0;
}

static AudArtItem * art_item_get_locked (const String & filename, bool * queued)
{
    if (queued)
        * queued = false;

    // blacklist stdin
    if (! strncmp (filename, "stdin://", 8))
        return nullptr;

    AudArtItem * item = art_items.lookup (filename);

    if (item && item->flag)
    {
        if (check_for_user_art (filename, item, true) > 0)  // ALSO CHECK TAG FILE (MAY OVERWRITE ANY EMBEDDED ART FOUND)!
            item->flag = FLAG_DONE;

        item->refcount ++;
        return item;
    }

    if (! item)
    {
        item = art_items.add (filename, AudArtItem ());
        item->filename = filename;
        item->refcount = 1; /* temporary reference */

        int chkres = check_for_user_art (filename, item, false);
        if (chkres > 0)
        {
            item->flag = FLAG_SENT;  // ART FOUND IN TAG FILE, WE'RE DONE, DON'T SCAN NON-PLAYING ENTRY (SLOW)!
            return item;
        }
        else if (chkres == 0)
            scanner_request (new ScanRequest (filename, SCAN_IMAGE, request_callback));
        else
        {
            art_item_unref_locked (item);
            return nullptr;
        }
    }

    if (queued)
        * queued = true;

    return nullptr;
}

void art_cache_current (const String & filename, Index<char> && data, String && art_file)
{
    pthread_mutex_lock (& mutex);

    clear_current_locked ();

    AudArtItem * item = art_items.lookup (filename);

    if (! item)
    {
        item = art_items.add (filename, AudArtItem ());
        item->filename = filename;
        item->refcount = 1; /* temporary reference */
    }

    finish_item_locked (item, std::move (data), std::move (art_file));

    item->refcount ++;
    current_item = item;

    pthread_mutex_unlock (& mutex);
}

void art_clear_current ()
{
    pthread_mutex_lock (& mutex);
    clear_current_locked ();
    pthread_mutex_unlock (& mutex);
}

void art_cleanup ()
{
    auto queued = get_queued ();
    for (AudArtItem * item : queued)
        aud_art_unref (item); /* release temporary reference */

    /* playback should already be stopped */
    assert (! current_item);

    if (art_items.n_items ())
        AUDWARN ("Album art reference count not zero at exit (%d)!\n", art_items.n_items ());
}

EXPORT AudArtPtr aud_art_request (const char * file, int format, bool * queued)
{
    pthread_mutex_lock (& mutex);

    String key (file);
    AudArtItem * item = art_item_get_locked (key, queued);
    bool good = true;

    if (! item)
        goto UNLOCK;

    if (format & AUD_ART_DATA)
    {
        /* load data from external image file */
        if (item->data.len () <= 0 && item->art_file)
        {
            VFSFile file (item->art_file, "r");
            if (file)
                item->data = file.read_all ();
        }

        if (item->data.len () <= 0)
            good = false;
    }

    if (format & AUD_ART_FILE)
    {
        /* save data to temporary file */
        if (item->data.len () && ! item->art_file)
        {
            String local = write_temp_file (item->data.begin (), item->data.len ());
            if (local)
            {
                item->art_file = String (filename_to_uri (local));
                item->is_temp = true;
            }
        }

        if (! item->art_file)
            good = false;
    }

    if (! good)
    {
        art_item_unref_locked (item);
        item = nullptr;
    }

UNLOCK:
    pthread_mutex_unlock (& mutex);
    return AudArtPtr (item);
}

EXPORT const Index<char> * aud_art_data (const AudArtItem * item)
    { return & item->data; }
EXPORT const char * aud_art_file (const AudArtItem * item)
    { return item->art_file; }

EXPORT void aud_art_unref (AudArtItem * item)
{
    pthread_mutex_lock (& mutex);
    art_item_unref_locked (item);
    pthread_mutex_unlock (& mutex);
}
