#!/usr/bin/perl -w

### HOW TO COMPILE TO EXE (FOR M$-WINDOWS USERS WITHOUT PERL INSTALLED (NEARLY ALL)):
###pp --gui -o FauxdaciousCoverArtHelper.exe -M utf8_heavy.pl -M lyrichelper_modules -l libeay32_.dll -l zlib1_.dll -l ssleay32_.dll FauxdaciousCoverArtHelper.pl

###(lyrichelper_modules.pm contains):
###use Carp;
###use HTML::Strip;
###use URI::Escape;
###use HTML::Entities;
###use LWP::UserAgent;
###use LyricFinder;
###use LyricFinder::_Class;
###use LyricFinder::ApiLyricsOvh;
###use LyricFinder::AZLyrics;
###use LyricFinder::ChartLyrics;
###use LyricFinder::Genius;
###use LyricFinder::Letras;
###use LyricFinder::Musixmatch;
###1;

#FAUXDACIOUS "HELPER" SCRIPT TO FETCH COVER ART FOR CDs/DVSs FROM coverartarchive and dvdcover.com:

#USAGE:  $0 {CD[T] diskID | DVD title | ALBUM album} [configpath] artist title [NOWEB|image-URL]| DELETE COVERART configpath

#NOTE:  SEE ALSO THE CONFIG (.ini) FILE: FauxdaciousCoverArtHelper.ini in /contrib/, 
#       MOVE OVER TO ~/.config/fauxdacious[_instancename]/ AND EDIT TO SUIT YOUR NEEDS!

#CONFIGURE:  ~/.config/fauxdacious[_instancename]/config:  [audacious].cover_helper=FauxdaciousCoverArtHelper.pl

#THIS SCRIPT ATTEMPTS TO FETCH THE COVER ART FOR CDs AND DVDs AND, IF NEEDED, THE TRACK TITLES AND ARTISTS 
#FOR CDs.  IT ALSO HANDLES DELETING TEMPORARY COVER-ART IMAGES FOR FAUXDACIOUS.
#IT CURRENTLY SCRAPES SITES:  dvdcover.com, archive.org AND/OR musicbrainz.org FOR THE NEEDED INFORMATION.

#===================================================================================================

#STRIP OUT INC PATHS USED IN COMPILATION - COMPILER PUTS EVERYTING IN IT'S OWN
#TEMPORARY PATH AND WE DONT WANT THE RUN-TIME PHISHING AROUND THE USER'S LOCAL
#MACHINE FOR (POSSIBLY OLDER) INSTALLED PERL LIBS (IF HE HAS PERL INSTALLED)!
BEGIN
{
	$isExe = 1  if ($0 =~ /exe$/io);
	if ($isExe)
	{
		while (@INC)
		{
			$_ = shift(@INC);
			push (@myNewINC, $_)  if (/(?:cache|CODE)/o);
		}
		@INC = @myNewINC;
	}
	else
	{
		while (@INC)   #REMOVE THE "." DIRECTORY!
		{
			$_ = shift(@INC);
			push (@myNewINC, $_)  unless ($_ eq '.');
		}
		@INC = @myNewINC;
	}
}
use strict;
use File::Copy;
use HTML::Entities ();
use LWP::UserAgent ();
use URI::Escape;

die "..usage: $0 {CD[T] diskID | DVD title | ALBUM album} [configpath] [artist] | DELETE COVERART configpath\n"  unless ($ARGV[0] && $ARGV[1]);
my $configPath = '';

for (my $a=0;$a<=$#ARGV;$a++) {  #STRIP QUOTES AROUND ARGUMENTS OFF!:
	$ARGV[$a] =~ s/^\'//;
	$ARGV[$a] =~ s/\'$//;
}

if ($ARGV[1]) {
	($configPath = $ARGV[2]) =~ s#^file:\/\/##;
}

my $html = '';
my $title = '';
my $artist = '';
my $comment = '';
my @userAgentOps = ();
my $bummer = ($^O =~ /MSWin/);
my $DEBUG = defined($ENV{'FAUXDACIOUS_DEBUG'}) ? $ENV{'FAUXDACIOUS_DEBUG'} : 0;

push (@userAgentOps, 'agent', ($bummer
		? 'Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/78.0.3904.108 Safari/537.36'
		: 'Mozilla/5.0 (X11; Linux x86_64; rv:84.0) Gecko/20100101 Firefox/84.0'));
my $ua = LWP::UserAgent->new(@userAgentOps);
$ua->timeout(10);
$ua->cookie_jar({});
$ua->env_proxy;
if ($bummer && $configPath && $DEBUG) {  #WARNING:MAY HANG WHEN MULTITHREADED, BUT STUPID WINDOWS DOESN'T SHOW DEBUG OUTPUT ON TERMINAL!
	my $log_fh;
    open $log_fh, ">${configPath}/FauxdaciousCoverArtHelper_log.txt";
    *STDERR = $log_fh;
}
print STDERR "\n-CoverArtHelper(".join('|',@ARGV)."=\n"  if ($DEBUG);

if ($ARGV[0] =~ /^DELETE/ && $ARGV[1] =~ /^COVERART/ && $configPath) {  #WE'RE REMOVING ALL OLD COVERART IMAGE FILES:
	if (open TAGDATA, "<${configPath}/tmp_tag_data") {
		my ($comment, @fids);
		my $skipdisks = 0;
		while (<TAGDATA>) {
			if (m#^\[([^\]]+)#o) {
				my $one = $1;
				$skipdisks = ($one =~ m#^(?:cdda|dvd)\:\/\/#o) ? 1 : 0;
			} elsif (!$skipdisks && m#^Comment\=file\:\/\/\/(.+)#o) {  #THIS ELIMINATES ANY EXISTING CD TAGS (WILL ALL BE REPLACED!):
				$comment = $1;
				@fids = split(m#\;file\:\/\/\/#o, $comment);  #NOW MAY HAVE MORE THAN ONE (CHANNEL ICON)!
				foreach my $fid (@fids) {
					next  if ($fid =~ /[\*\?]/o);  #GUARD AGAINST WILDCARDS, ETC!!
					next  unless ($fid =~ /(?:png|jpe?g|gif|com|webp)$/io);  #MAKE SURE WE ONLY DELETE IMAGE FILES!
					$fid =~ s/^(\w)\%3A/$1\:/;
					if ($bummer && $fid =~ /^\w\:/) {   #BUMMER, WE'RE ON WINDOWS:
						$fid =~ s#\/#\\#go;  #MAY NOT BE NECESSARY?
					} else {
						$fid = '/' . $fid  unless ($fid =~ m#^\/#o);
					}
					print STDERR "i:$0: DELETED temp. image file ($fid)!\n";
					unlink $fid;
				}
			}
		}
		close TAGDATA;
	}	
} elsif ($ARGV[0] =~ /^CD/i) {  #WE'RE AN AUDIO-CD: LOOK UP DISK-ID ON Musicbrainz.com:
	print STDERR "-0($ARGV[0]): URL=https://musicbrainz.org/otherlookup/freedbid?other-lookup.freedbid=$ARGV[1]\n"  if ($DEBUG);
	my $response = $ua->get("https://musicbrainz.org/otherlookup/freedbid?other-lookup.freedbid=$ARGV[1]");
	if ($response->is_success) {
		$html = $response->decoded_content;
	} else {
		print STDERR $response->status_line  if ($DEBUG);
		print STDERR "! (https://musicbrainz.org/otherlookup/freedbid?other-lookup.freedbid=$ARGV[1])\n";
	}
	die "f:failed to find title ($ARGV[1]) on musicbrainz.com!\n"  unless ($html);

	my $url2 = '';
	my ($tmpurl2, $tmptitle, $tmpartist);

	while ($html =~ s#^.*?\<tr[^\>]*\>##is)  #MAY BE MULTIPLE MATCHES, TRY TO FIND ONE THAT'S A "CD" (VS. A "DVD", ETC.):
	{
#x		$html =~ s#^.*?\<tr[^\>]*\>##is;
		if ($html =~ s#^.*?\<a\s+href\=\"([^\"]+)\"\>\s*\<bdi\>([^\<]+)\<\/bdi\>##is) {
			$tmpurl2 = $1;
			$tmptitle = $2;
			if ($html =~ s#^.*?\<bdi\>([^\<]+)\<\/bdi\>##is)
			{
				$tmpartist = $1;
				print STDERR "-1a: artist=$tmpartist= url=$tmpurl2= title=$tmptitle=\n"  if ($DEBUG);
				$html =~ s#^.*?\<td[^\>]*\>##is;
				if ($html =~ s#^CD\<\/td\>##is) {   #FOUND A DEFINITE "CD" (GRAB & QUIT!):
					$url2 = $tmpurl2;   #URL OF PAGE CONTAINING DETAILS FOR THE PARTICULAR CD:
					$title = HTML::Entities::decode_entities($tmptitle);
					$artist = HTML::Entities::decode_entities($tmpartist);
					print STDERR "-1a: CD url=$url2= title=$title= artist=$artist=\n"  if ($DEBUG);
					last;
				} elsif ($html =~ s#^([^C]+CD|\(unknown\))\<\/td\>##is) {  #FOUND WHAT MIGHT BE A CD, KEEP LOOKING:
					$url2 = $tmpurl2;
					$title = HTML::Entities::decode_entities($tmptitle);
					$artist = HTML::Entities::decode_entities($tmpartist);
				} else {    #SKIP THIS ROW:
					$html =~ s#^[^\<]*\<\/td\>##is;
				}
				next;
			}
		}
		else
		{
			last;
		}
	}
	if ($url2) {   #NOW FETCH THE DETAILS (COVER ART & TRACK TITLES) FOR THE MATCHED CD:
		my @tracktitle = ();
		my @trackartist = ();
		my $art_url = '';
		$url2 = 'https://musicbrainz.org' . $url2  unless ($url2 =~ /^http/);
		print STDERR "-2: URL2=$url2=\n"  if ($DEBUG);
		$art_url = &lookup_art_on_mb_release_by_mbid($url2);
		print STDERR "-4: cover url=$art_url=\n"  if ($DEBUG);
		my $trk = 0;
		my $haveTrackData = 0;
		if ($ARGV[0] =~ /^CDT/i) {  #WE NEED TRACK TITLES ALSO, FIND & GRAB 'EM ALL:
			my @titlerows = split(m#<a\s+href\=\"(?:https\:\/\/musicbrainz\.org)?\/track\/[^\"]+\"\>#s, $html);
			shift @titlerows;  #REMOVE STUFF ABOVE 1ST TITLE.
			while (@titlerows) {
				my $rowhtml = shift @titlerows;
				next  unless ($rowhtml =~ m#^\d+\<\/a\>\s*\<\/td\>[^\<]*\<td.*?\<bdi>([^\<]+)#s);

				my $t = HTML::Entities::decode_entities($1);  #WE MUST DE-HTML & MAKE ALL-ASCII FOR Fauxdacious:
				print STDERR "--track$trk($1)=$t=\n"  if ($DEBUG);   #WE MAKE ASCII BY REMOVING ALL ACCENTS!:
				$t =~ s/[\x80-\xff]//g;
				$tracktitle[$trk] = $t;
				$haveTrackData++;
				$trackartist[$trk] = '';
				while ($rowhtml =~ s#\<a\s+href\=\"\/artist\/[^\"]*\"\s+title\=\"[^\"]*\"\>\<bdi\>([^\<]*)\<\/bdi\>##s) {
					my $t = HTML::Entities::decode_entities($1);
					$t =~ s/[\x80-\xff]//g;
					$trackartist[$trk] .= "$t, ";
				}
				$trackartist[$trk] =~ s/\, $//o;
				$trackartist[$trk] = $artist  unless ($trackartist[$trk] =~ /[a-z]/io);
				print STDERR "-4.1: artist($trk)=$trackartist[$trk]=\n"  if ($DEBUG);
				++$trk;
			}
		}
		
		$comment = "Album=$title\n";
		$comment .= "AlbumArtist=$artist\n"  if ($artist);
		#x$comment .= "Artist=$artist\n"  if ($artist);
		if ($art_url) {   #WE FOUND AN IMAGE LINK TO WHAT SHOULD BE "COVER-ART":
			my $image_ext = ($art_url =~ /\.(\w+)$/) ? $1 : 'jpg';
			my $art_image;
			print STDERR "-5: ext=$image_ext= art_url=$art_url= configpath=$configPath=\n"  if ($DEBUG);
			if ($configPath && -e "${configPath}/$ARGV[1].$image_ext") {  #IF WE ALREADY HAVE THIS IMAGE, DON'T RE-DOWNLOAD IT!:
				print STDERR "-5a: already have art image (${configPath}/$ARGV[1].$image_ext)!\n"  if ($DEBUG);
				my $path = $configPath;
				if ($bummer && $path =~ m#^\w\:#) { #WE'RE ON M$-WINDOWS, BUMMER: :(
					$path =~ s#^(\w)\:#\/$1\%3A#;
					$path =~ s#\\#\/#g;
				}
				$comment .= "Comment=file://${path}/$ARGV[1].$image_ext\n";
			} else {   #WE DON'T HAVE IT, SO DOWNLOAD IT:
				print STDERR "-5b: attempt to download art image (${configPath}/$ARGV[1].$image_ext)!\n"  if ($DEBUG);
				my $response = $ua->get($art_url);
				if ($response->is_success) {
					$art_image = $response->decoded_content;
				} else {
					print STDERR $response->status_line;
					print STDERR "! ($art_url)\n";
				}
				#NOW WRITE OUT THE DOWNLOADED IMAGE TO A FILE Fauxdacious CAN FIND:
				if ($configPath && $art_image && open IMGOUT, ">${configPath}/$ARGV[1].$image_ext") {
					print STDERR "-6: will write art image to (${configPath}/$ARGV[1].$image_ext)\n"  if ($DEBUG);
					binmode IMGOUT;
					print IMGOUT $art_image;
					close IMGOUT;
					my $path = $configPath;
					if ($bummer && $path =~ m#^\w\:#) { #WE'RE ON M$-WINDOWS, BUMMER: :(
						$path =~ s#^(\w)\:#\/$1\%3A#;
						$path =~ s#\\#\/#g;
					}
					$comment .= "Comment=file://${path}/$ARGV[1].$image_ext\n";
				}
			}
		}
		&writeTagData($comment, \@tracktitle, \@trackartist)  #WRITE OUT ALL THE TRACK TAGS TO tmp_tag_data:
				if ($haveTrackData);  # (CDT)
		exit (0);
	}
} elsif ($ARGV[0] =~ /^DVD/i) {   #WE'RE A DVD:  JUST LOOK UP AND DOWNLOAD THE COVER-ART:
	foreach my $image_ext (qw(jpg png gif jpeg)) {
		if ($configPath && -e "${configPath}/$ARGV[1].$image_ext") {
			print STDERR "-DVD: done:Cover art file (${configPath}/$ARGV[1].$image_ext) found locally.\n"  if ($DEBUG);
			exit (0);
		}
	}
	(my $argv1clean = $ARGV[1]) =~ s/[\x00-\x1f]//g;
	my $argv1unclean = $argv1clean;
	my $art_url = '';

	print STDERR "-1a: URL=https://dvdcover.com/?s=$argv1clean=\n"  if ($DEBUG);
OUTER:		for (my $try=0;$try<=1;$try++) {
		$html = '';
		print STDERR "-1a TRY($try) of 2: argv=$argv1clean=\n"  if ($DEBUG);
		my $response = $ua->get("https://dvdcover.com/?s=$argv1clean");
		if ($response->is_success) {
			$html = $response->decoded_content;
		} else {
			print STDERR $response->status_line;
			print STDERR "! (https://dvdcover.com/?s=$argv1clean)\n";
			$html = '';
		}
		print STDERR "--falling back to WGET (wget -t 2 -T 20 -O- -o /dev/null \"https://dvdcover.com/?s=$argv1clean\" 2>/dev/null)!...\n"
				if ($DEBUG && !$bummer && !$html);
		$html ||= `wget -t 2 -T 20 -O- -o /dev/null \"https://dvdcover.com/?s=$argv1clean\" 2>/dev/null `
				unless ($bummer);
		if ($html =~ m#^.+\<ul\s+id\=\"infinite\-articles\"(.+)?\<\/ul\>#s) {
			my $html0 = $1;
			$html0 =~ s#\<\/ul\>.+##s;
			my $imghtml = '';
			$html = $html0;
			my $entryhtml = '';
			#MATCHES DO NOT EXACTLY MATCH BASED ON DVD_TITLE FROM libdvdread, SO WE HAVE TO SEARCH/GUESS BEST ONE!:
			while ($html =~ s#(\<li\s+.+?\<\/li\>)##so) {  # 1ST CHECK IF DESCRIPTION MATCHES TITLE & IS DVD COVER:
				$entryhtml = $1;
				if ($entryhtml =~ /\bdescription\=\"?([^\"]+)/i) {
					my $desc = $1;
					(my $title = $argv1clean) =~ s/\_/ /g;
					print STDERR "-1:SEARCH DESCRIPTION- DESC=$desc= TITLE=$title=\n"  if ($DEBUG);
					if ($desc =~ /$title/i && $desc =~ /DVD Cover/i) {
						if ($entryhtml =~ m#\<img\s+([^\>]+)\>#i) {
							my $imghtml = $1;
							$art_url = $1  if ($imghtml =~ m#src\=\"([^\"]+)#);
							print STDERR "---GOT IT1! ($art_url)\n"  if ($DEBUG);
							last   if ($art_url);
						}
					}
				}
			}
			unless ($art_url) {
				$html = $html0;
				while ($html =~ s#(\<li\s+.+?\<\/li\>)##so) { # 2ND CHECK IF TAGGED AS DVD COVER:
					$entryhtml = $1;
					if ($entryhtml =~ /\<p\>\<p\>([^\<]+)/i) {
						my $tagshtml = $1;
						print STDERR "-2:SEARCH TAGS- TAGS=$tagshtml=\n"  if ($DEBUG);
						if ($tagshtml =~ /DVD Cover/i) {
							if ($entryhtml =~ m#\<img\s+([^\>]+)\>#i) {
								my $imghtml = $1;
								$art_url = $1  if ($imghtml =~ m#src\=\"([^\"]+)#);
								print STDERR "---GOT IT2! ($art_url)\n"  if ($DEBUG);
								last   if ($art_url);
							}
						}
					}
				}
			}
			unless ($art_url) {  # 3RD CHECK IF IMAGE URL ITSELF CONTAINS THE TITLE
				$html = $html0;
				while ($html =~ s#(\<li\s+.+?\<\/li\>)##so) {
					$entryhtml = $1;
					if ($entryhtml =~ m#\<img\s+([^\>]+)\>#i) {
						my $imghtml = $1;
						if ($imghtml =~ /$argv1clean/i) {
							$art_url = $1  if ($imghtml =~ m#src\=\"([^\"]+)#);
							print STDERR "---GOT IT3! ($art_url)\n"  if ($DEBUG);
							last   if ($art_url);
						}
					}
				}
			}
			unless ($art_url) { # LAST CHECK IF THE REFERENCE LINK CONTAINS THE TITLE:
				(my $key = $argv1clean) =~ s/\_/\-/g;
				$html = $html0;
				while ($html =~ s#(\<li\s+.+?\<\/li\>)##so) {
					$entryhtml = $1;
					if ($entryhtml =~ m#\<a\s+href\=\"([^\"]+)\"\>\s+\<img\s+([^\>]+)\>#i) {
						my $link = $1;
						my $imghtml = $2;
						print STDERR "-4:SEARCH REF. LINK- KEY=$key= link=$link=\n"  if ($DEBUG);
						if ($link =~ m#\/$key#i) {
							$art_url = $1  if ($imghtml =~ m#src\=\"([^\"]+)#);
							print STDERR "---GOT IT4! ($art_url)\n"  if ($DEBUG);
							last   if ($art_url);
						}
					}
				}
			}
		}
		$argv1clean =~ s/\_\d+x.*$//i;  #TRY AGAIN W/O EXTRA DISK OR SIZE INFO IN TITLE:
		$argv1clean =~ s/\_DISC.*$//;
		last  if ($argv1clean eq $argv1unclean);
	}
	&writeArtImage($1, $ARGV[1])  if ($art_url);  #GOT AN IMAGE LINK TO (HOPEFULLY THE RIGHT) COVER-ART IMAGE:

#SEEMS TO BE DEPRECIATED, BUT NOT CERTAIN, SO LEAVING FOR NOW:
PLAN_B:   #PLAN "B":  NOT ON Dvdcover.com, SO LET'S TRY Archive.org (Dvdcover easier & has better DVD images, but I think Archive.org has more):
	print STDERR "-1(PLAN B): URL=https://archive.org/details/coverartarchive?and[]=$argv1clean=\n"  if ($DEBUG);
	$argv1clean = $argv1unclean;
	for (my $try=0;$try<=1;$try++) {
		print STDERR "-1a TRY($try) of 2: argv=$argv1clean=\n"  if ($DEBUG);
		my $response = $ua->get("https://archive.org/details/coverartarchive?and[]=$argv1clean");
		if ($response->is_success) {
			$html = $response->decoded_content;
		} else {
			print STDERR $response->status_line  if ($DEBUG);
			print STDERR "! (https://archive.org/details/coverartarchive?and[]=$argv1clean)\n"  if ($DEBUG);
		}
		print STDERR "-1a TRY($try) of 2: HTML=$html=\n"  if ($DEBUG > 2);
		if ($html && $html =~ s#\<a\s+href\=\"\/details\/mbid\-([^\"]+)\"\s+title\=\"([^\"]+)\"##is) {
			my $mbid = $1;
			$title = $2;
			print STDERR "-2a: mbid=$mbid= title=$title=\n"  if ($DEBUG);
			if ($html =~ s#\<img\s+class\=\"item\-img\s*\"\s+source\=\"(\/services\/img\/mbid\-$mbid)\"##is) {
				$art_url = 'https://archive.org' . $1;
				print STDERR "-3: cover url=$art_url=\n"  if ($DEBUG);
			}
			last;
		}
		$argv1clean =~ s/\_\d+x.*$//;  #TRY AGAIN W/O EXTRA DISK OR SIZE INFO IN TITLE:
		$argv1clean =~ s/\_DISC.*$//;
		last  if ($argv1clean eq $argv1unclean);
	}
	print STDERR "-4: title=$title= cover url=$art_url=\n"  if ($DEBUG);
	die "f:No cover art url found!\n"  unless ($art_url);

	&writeArtImage($art_url, $ARGV[1])  if ($art_url);   #GOT AN IMAGE LINK TO (HOPEFULLY THE RIGHT) COVER-ART IMAGE:
}
elsif ($ARGV[0] =~ /^ALBUM/i)   #WE'RE AN ALBUM TITLE, GET COVER ART FROM TAGS, LYRICSFINDER, OR MUSICBRAINZ:
{
	### NOTE:  FAUXDACIOUS NOW CHECKS CACHE, SO IF WE'RE HERE, WE HAVE TO SEARCH THE WEB!:
	if ($configPath && -d $configPath) {
		unlink("${configPath}/_tmp_lyrics_from_albumart.txt")  if (-e "${configPath}/_tmp_lyrics_from_albumart.txt");
		unlink("${configPath}/_albumart_done.tmp")  if (-e "${configPath}/_albumart_done.tmp");
	}
	(my $album = $ARGV[1]) =~ s/\%20$//;  #WHACK OFF TRAILING SPACE.
	my $artist = defined($ARGV[3]) ? $ARGV[3] : '_';
	my $title = defined($ARGV[4]) ? $ARGV[4] : '_';
	#NO LONGER NEEDED?: $title =~ s/\%7c\%7c.*$//i;  #HANDLE SOME UGLY TITLES IN FORMAT:  "<title> || blah blah .."
	$title =~ s/\%20$//;  #WHACK OFF TRAILING SPACE.
	$artist =~ s/\%20$//;  #WHACK OFF TRAILING SPACE.
	my $albart_FN = $album;    #FORMAT FILENAME AS:  <album>[__<artist>] (AS SOME ALBUMS FROM DIFFERENT ARTISTS SHARE SAME NAME)!
	if ($artist =~ /\S/ && $artist ne '_') {
		$albart_FN .= "__$ARGV[3]";
	} elsif ($title =~ /\S/ && $title ne '_') {
		$albart_FN .= "__$ARGV[4]";
	}
	$albart_FN =~ s/\%20$//;      #WHACK OFF TRAILING SPACE.
	$albart_FN =~ s/\%20/ /g;     #UNESCAPE OTHER SPACES.
	$albart_FN =~ s#\s*\/\s*#\_#; #ELIMINATE SLASHES (FILE NAME IS *NOT* A PATH!
	if ($configPath) {
		foreach my $ext (qw(jpg png gif jpeg)) {  #REMOVE ANY EXISTING TEMP. ART-IMAGE FILES:
				unlink ("${configPath}/_tmp_albumart.$ext")  if (-e "${configPath}/_tmp_albumart.$ext");
		}
	}

	my %SKIPTHESE = ();
	if ($configPath) {
		## USER-CONFIGURED SITE-SKIP LIST:
		if (open IN, "<${configPath}/FauxdaciousCoverArtHelper.ini") {
			while (<IN>) {
				s/\r//go;
				chomp;
				next  if (/^\#/o);
				next  unless (/\S/o);
				s/^\s+//o;
				s/\,\s*$//o;
				my $key = (s/^(\w+?)\s*\=\s*//o) ? $1 : 'skip';
				s/^[\'\"]//o;
				s/[\'\"]$//o;
				push @{$SKIPTHESE{$key}}, $_;
			}
			close IN;
		}
	}

	(my $album_uesc = uri_unescape($album)) =~ s/\s+/ /g;  #SOMETIMES HAVE CONTIGUOUS SPACES.
	my $artist_uesc = uri_unescape($artist);
	my $title_uesc = uri_unescape($title);
	print STDERR "i:ART HELPER: DOING: ALBUM=$album_uesc= TITLE=$title_uesc= ARTIST=$artist_uesc=\n"  if ($DEBUG);
	foreach my $skipit (@{$SKIPTHESE{'skip'}}) {
		$skipit =~ s/\|\_$/\|$title_uesc/;  #WILDCARDS:
		$skipit =~ s/^\_\|/$album_uesc\|/;
		$skipit =~ s/\|/\\\|/;
		if ("$album_uesc|$title_uesc" =~ /^${skipit}/i) {
			print STDERR "i:ART HELPER: SKIPPING ($skipit) AS CONFIGURED.\n"  if ($DEBUG);
			&albumart_done();
			exit(0);  #QUIT - USER DOES NOT WISH TO LOOK UP *THIS* ALBUM NAME/TITLE THOUGH!
		}
	}

	#STEP 1:  WE HAVE AN "IN-STREAM" ART LINK FROM TAGS (IN $ARGV[5]), TRY THAT:

	my $art2fid = "albumart/${albart_FN}";
	if (defined $ARGV[5] && $ARGV[5] =~ /\S/o) {
		if ($ARGV[5] =~ m#^file\:\/\/#) {  #FILE (FAUXD. ENTRY), SAVE IT TO THIS FILE INSTEAD OF CACHE:
		    ($art2fid = $ARGV[5]) =~ s#^file\:\/\/##;
		    $art2fid =~ s/\.[^\.]*$//;   #STRIP OFF MEDIA EXTENSION!
print STDERR "---Save any albumart found to LOCAL FIDBASE=$art2fid=\n"  if ($DEBUG);
		} elsif ($ARGV[5] !~ m#^https?\:\/\/#) { #("NOWEB") - DONE: CHECKED CACHE, BUT DON'T SEARCH WEB!
			print STDERR "d:ART HELPER: SKIPPING (URL ALBUM/NOWEB - SHOULD BE CAUGHT IN FAUXD NOW?($ARGV[5])!)\n"  if ($DEBUG);
			&albumart_done();
			exit(0);
		} else {
			#OTHERWISE, ASSUME WE HAVE A COVER-ART URL FROM TAG-DATA, SO GRAB IT INSTEAD OF SEARCHING!
			foreach my $skipit (@{$SKIPTHESE{'notagart'}}) {  #FIRST CHECK FOR notagart="image-url", QUIT ON MATCH.
				if ($ARGV[5] =~ /^\Q${skipit}\E$/) {
					print STDERR "i:ART HELPER: SKIPPING ART LOOKUP FOR THIS TAG-URL ($skipit) AS CONFIGURED.\n"  if ($DEBUG);
					&albumart_done();  #QUIT - USER DOES NOT WISH TO USE METATAG ART OR SEARCH WEB FOR *THIS* ART URL!
					exit(0);
				}
			}
			print STDERR "i:FETCHING ($ARGV[5]) FOUND IN STREAM!\n"  if ($DEBUG);
		}
		foreach my $skipit (@{$SKIPTHESE{'notagart'}}) {  #NEXT CHECK FOR notagart="album/title", SKIP & SEARCH WEB INSTEAD ON MATCH.
			$skipit =~ s/\|\_$/\|$title_uesc/;  #WILDCARDS:
			$skipit =~ s/^\_\|/$album_uesc\|/;
			if ("$album_uesc|$title_uesc" =~ /^\Q${skipit}\E$/) {
				print STDERR "i:ART HELPER: NOT USING ART TAG-URL FOR ($skipit) AS CONFIGURED...\n"  if ($DEBUG);
				goto WEBSEARCH;  #USER DOES NOT WISH TO USE STATION-SUPPLIED ART URL FOR *THIS* ALBUM NAME/TITLE, BUT SEARCH WEB!
			}
		}
		unless (-d "${configPath}/albumart") {
			mkdir ("${configPath}/albumart",0755) || die "f:Could not create directory (${configPath}/albumart) ($!)!\n";;
		}
		if ($ARGV[5] =~ /^http/) {
			&albumart_done();
			&writeArtImage($ARGV[5], "albumart/${albart_FN}", '_tmp_albumart');  #EXITS IF SUCCESSFUL!
			unlink("${configPath}/_albumart_done.tmp");  #NOPE, IF WE GOT HERE: ALBUMART *NOT* DONE, SEARCH WEB!
		}
    }

WEBSEARCH:
	#LAST DITCH CHECK CACHE FOR BLACKLISTED ARTIST OR ALBUM:
	my $release = $album;
	$release =~ s/(?:\%20)+$//o;    #CHOMPIT (TRAILING ESCAPED SPACES)!
	$release =~ s/\%C2\%B4/\%27/g;  #FIX UNICODE QUOTES.
	foreach my $skipit (@{$SKIPTHESE{'noalbum'}}) {
		$skipit = uri_escape($skipit);
		if ($release =~ /^$skipit/i) {
			print STDERR "i:ART HELPER: SKIPPING ALBUM ($skipit) AS CONFIGURED.\n"  if ($DEBUG);
			&albumart_done();
			exit(0);  #QUIT - USER DOES NOT WISH TO LOOK UP *THIS* ALBUM NAME THOUGH!
		}
	}
	foreach my $skipit (@{$SKIPTHESE{'noartist'}}) {
		$skipit = uri_escape($skipit);
		if ($artist =~ /^$skipit/i) {
			print STDERR "i:ART HELPER: SKIPPING ARTIST ($skipit) AS CONFIGURED.\n"  if ($DEBUG);
			&albumart_done();
			exit(0);  #QUIT - USER DOES NOT WISH TO LOOK UP *THIS* ARTIST NAME THOUGH!
		}
	}

	print STDERR "---ART HELPER WILL SEARCH THE WEB...\n"  if ($DEBUG);
	my ($url, $response, $mbzid, $art_url, $arthtml, %mbHash, $priority);
	my $tried = '';
	unless (-d "${configPath}/albumart") {
		mkdir ("${configPath}/albumart",0755) || die "f:Could not create directory (${configPath}/albumart) ($!)!\n";;
	}

	#STEP 2:  TRY LYRICFINDER'S SITES THAT CAN RETURN COVER-ART LINKS FIRST, AS IT IS FAST (SIMPLEST LOOKUP)!:
	#(REQUIRES BOTH AN ARTIST AND A TITLE, NOT JUST ARTIST+ALBUM):

	if ($artist ne '_' && $title ne '_') {
		#FIRST TRY LYRICFINDER (WHICH CAN ALSO QUICKLY FETCH ALBUM ART (+LYRICS), WHICH MAY ALSO BE NEEDED):
		my $haveLyricFinder = 0;
		eval "use LyricFinder; \$haveLyricFinder = \$LyricFinder::VERSION; 1";
		if ($haveLyricFinder >= 1.2) {
			my $lf = new LyricFinder(-omit => 'ApiLyricsOvh,AZLyrics,Letras'); #OMIT LYRICS SITES THAT DON'T RETURN ALBUMART:
			if ($lf) {
				my $artist = uri_unescape($ARGV[3]);
				my $title = uri_unescape($ARGV[4]);
				my $lyrics = $lf->fetch($artist, $title, 'random');
				if (defined($lyrics) && $lyrics) {
					my $image_url = $lf->image_url();
					my $doschar = ($^O =~ /Win/) ? "\r" : '';
					$lyrics .= "${doschar}\n(Lyrics courtesy: ".$lf->source().")${doschar}\n".$lf->site();
					if ($configPath && open OUT, ">${configPath}/_tmp_lyrics_from_albumart.txt") {
						binmode OUT;
						print OUT $lyrics;  #MUST NULL-TERMINATE FOR FAUXDACIOUS'S C CODE!
						close OUT;
					}
					print STDERR "i:found (".$lf->source().") IMAGE ($image_url)?\n"  if ($DEBUG);
					$tried = $lf->tried();
					&albumart_done($tried);
					&writeArtImage($image_url, $art2fid, '_tmp_albumart')
							if ($image_url && $image_url !~ /(?:default|place\-?holder|nocover)/i); #EXCLUDE DUMMY-IMAGES.
				} else {
					$tried = $lf->tried();
				}
			}
		} else {
			warn "w:FauxdaciousCoverArtHelper needs LyricFinder v1.2 or better for best results!";
		}
	}
	&albumart_done($tried);

	#STEP 3:  SEARCH MUSICBRAINZ - BY ALBUM, THEN TITLE (SLOWER BUT MORE COMPREHENSIVE):

	#LYRICFINDER DID NOT RETURN A COVER IMAGE, SO SEARCH MUSICBRAINZ (FIRST TRY ARTIST/ALBUM, THEN ARTIST/TITLE):
	#USER NOTE:  IF YOU MOSTLY JUST LISTEN TO STREAMING STATIONS (THAT USUALLY JUST SET THE ALBUM FIELD
	#TO THE STATION NAME), YOU MIGHT WANT TO REVERSE THIS LIST (IE. $title, /* then */ $album)
	#FOR EFFICIENCY'S SAKE!!

RELEASETYPE:
	foreach my $release ($album, $title) {
		next  if ($release eq '_');
		chomp $release;
		$release =~ s/(?:\%20)+$//o;    #CHOMPIT (TRAILING ESCAPED SPACES)!
		$release =~ s/\%C2\%B4/\%27/g;  #FIX UNICODE QUOTES.
		$html = '';
		$url = "https://musicbrainz.org/taglookup?tag-lookup.artist=$artist&tag-lookup.release=$release";
		print STDERR "i:ART HELPER:SEARCHURL=$url=\n"  if ($DEBUG);
		$response = $ua->get($url);
		if ($response->is_success) {
			$html = $response->decoded_content;
		} else {
			print STDERR $response->status_line;
			print STDERR "! ($url)\n";
			$html = '';
		}
		next  unless ($html);

		my $artistEscaped = uri_unescape($artist);
		$artistEscaped = HTML::Entities::encode_entities($artistEscaped, '<>&"()');
		$release = uri_unescape($release);
		$release = HTML::Entities::encode_entities($release, '<>&"');
		print STDERR "i:release=$release= artist=$artistEscaped=\n"  if ($DEBUG);
		$mbzid = '';
		%mbHash = ();
		$html =~ s/\x{2019}/\'/gs;  #THEY HAVE SOME STUPID UNICODE SINGLE-QUOTES SOMETIMES!
		$html =~ s/\&\#x27\;/\'/gs; #AND OTHER TIMES THEY ESCAPE THEM, SO CONVERT TO COMMON FORMAT FOR NEXT REGEX!:
		while ($html =~ s#\<tr\s+class\=\"(?:odd|even)\"\s+data\-score\=\"\d*\"\>\<td\>\<a\s+href\=\"\/release\/[0-9a-f\-]+\/cover-art\"\>\<span\s+class\=\"caa\-icon\"\s+title\=\"This release has artwork in the Cover Art Archive\"\>\<\/span\>\<\/a\>\<a\s+href\=\"\/release\/([0-9a-f\-]+)\"\>\<bdi\>\Q${release}\E(.*?)\<\/tr\>##is) {
			$mbzid = $1;
			my $rowhtml = $2;
			print STDERR "---found MBZID1=$mbzid=\n"  if ($DEBUG);
			my $primaryArtistFactor = 20;
			while ($rowhtml =~ s#^.*?\<bdi\>([^\<]+)\<\/bdi\>##iso)
			{
				(my $thisartist = $1) =~ s/\&\#x([0-9A-Fa-f]{2})\;/chr(hex($1))/eg;  #THEY HAVE SOME "&#X..;" STUFF TOO!
				$primaryArtistFactor -= 3;
				print STDERR "i:ART:found ARTIST=$thisartist= ESCAPED=$artistEscaped=\n"  if ($DEBUG > 1);
				next  unless ($artistEscaped =~ /$thisartist/i
						|| $thisartist =~ /(?:$artistEscaped|various)/i);

				$priority = 0;
				$priority += $primaryArtistFactor if ($primaryArtistFactor > 0
						&& ($thisartist =~ /^$artistEscaped/i
						|| $artistEscaped =~ /^$thisartist/i));
				$rowhtml =~ s#^.*?\<td\>##iso;
				if ($rowhtml =~ m#([^\<]+)\<\/td\>#iso) {
					my $type = $1;
					print STDERR "i:ART:TYPE=$type=\n"  if ($DEBUG);
					next  if ($type =~ m#(?:cassette|\>)#io);   #SKIP CASSETTES (CRAPPY VERTICAL IMAGES)!:

					++$priority  unless ($thisartist =~ /various/io);
					++$priority  unless ($type =~ /unknown/io);
					++$priority  if ($type =~ /CD/o);   #CDs GET HIGHER PRIORITY.
					++$priority  if ($type =~ /^CD$/o); #SINGLE-CD COVERS GET EVEN HIGHER PRIORITY.
					print STDERR "i:*** ART:KEEPING TYPE=$type= ARTIST:$thisartist PRIO:$priority!\n"  if ($DEBUG);
				}
				$mbHash{$mbzid} = $priority;
			}
		}
		print STDERR '..ART: '.scalar(%mbHash)." potential Musicbrainz entries found...\n"  if ($DEBUG);
		#LOOK UP ART LINKS FOR EACH MATCHING MUSICBRAINZ ID IN ORDER OF DESCENDING PRIORITY, STOPPING IF ONE FOUND:
		foreach $mbzid (sort { $mbHash{$b} <=> $mbHash{$a} } keys %mbHash) {
			$art_url = &lookup_art_on_mb_release_by_mbid("https://musicbrainz.org/release/$mbzid");
			&writeArtImage($art_url, $art2fid, '_tmp_albumart')  if ($art_url);
		}
		print STDERR "-----ART:AT END OF FOR-LOOP($release), CONTINUE OR PUNT...\n"  if ($DEBUG > 1);
	}
	print STDERR "w:NO COVER-ART FOUND ON MUSICBRAINZ!\n\n"  if ($DEBUG);
	exit (0);
}
else
{
	die "f:usage: $0 {CD[T] diskID | DVD title | ALBUM album} [configpath] [artist] | DELETE COVERART configpath\n";
}

exit(0);

sub lookup_art_on_mb_release_by_mbid {
	my $url = shift;

	$html = '';
	my $art_url = '';

	print STDERR "ART:FIRST TRY ($url)...\n"  if ($DEBUG);
	my $response = $ua->get($url);
	if ($response->is_success) {
		$html = $response->decoded_content;
	} else {
		print STDERR $response->status_line;
		print STDERR "! ($url)\n";
	}
	#TRY TO FIND THE COVER-ART THUMBNAIL URL:
	foreach my $i (qw(small medium large huge)) {
		if ($html =~ s/data\-$i\-thumbnail\=\"([^\"]+)\"//) {
			$art_url = $1;
			$art_url = 'https:' . $art_url  if ($art_url =~ m#^\/\/#);
			print STDERR "ART:FOUND ($i) THUMBNAIL($art_url)!\n"  if ($DEBUG);
			return $art_url;
		}
	}
	if ($html =~ s/data\-fallback\=\"([^\"]+)\"//) {
		$art_url = $1;
		$art_url = 'https:' . $art_url  if ($art_url =~ m#^\/\/#);
		print STDERR "ART:FOUND FALLBACK IMG($art_url)!\n"  if ($DEBUG);
		return $art_url;
	} elsif ($html =~ s#\<div\s+class\=\"cover\-art\"[^\>]?\>\s*\<img([^\>]+)\>##so) {
		my $imghtml = $1;
		if ($imghtml =~ m#src\=\"([^\"]+)#so) {
			$art_url = $1;
			$art_url = 'https:' . $art_url  if ($art_url =~ m#^\/\/#);
			print STDERR "ART:found (AMAZON?) IMAGE ($1)?\n"  if ($DEBUG);
			return $art_url;
		}
	}
	my $html2 = '';
	$url .= '/cover-art';
	print STDERR "ART:NOW TRY ($url)...\n"  if ($DEBUG);
	$response = $ua->get($url);
	if ($response->is_success) {
		$html2 = $response->decoded_content;
	} else {
		print STDERR $response->status_line;
		print STDERR "! ($url)\n";
	}
	if ($html2 =~ s#class\=\"artwork-image\"\s+href\=\"([^\"]+)\"##s) {
		$art_url = $1;
		$art_url = 'https:' . $art_url  if ($art_url =~ m#^\/\/#);
		print STDERR "i:ART:FOUND ALT COVERART ARCHIVE IMAGE ($art_url)\n";
	}

	return $art_url;
}

sub writeArtImage {   # DOWNLOADS AND SAVES THE FOUND COVER-ART IMAGE AND EXITS:
	my ($art_url, $filename, $tee2tmp) = @_;

	return  unless ($art_url =~ /\S/);

	if ($bummer) {
		$filename = $configPath . '/' . $filename  unless ($filename =~ m#^(?:\w\:)?[\/\\]#o);
	} else {
		$filename = $configPath . '/' . $filename  unless ($filename =~ m#^\/#o);
	}
	my $image_ext = ($art_url =~ /\.(\w+)$/) ? $1 : 'jpg';
	my $art_image = '';
	print STDERR "-5: ext=$image_ext= art_url=$art_url= configpath=$configPath=\n"  if ($DEBUG);
	print STDERR "-5b: attempt to download art image (${filename}.$image_ext)!\n"  if ($DEBUG);
	my $response = $ua->get($art_url);
	if ($response->is_success) {
		$art_image = $response->decoded_content;
	} else {
		print STDERR $response->status_line;
		print STDERR "! ($art_url)\n";
		$art_image = '';
	}
	if ($configPath && $art_image && open IMGOUT, ">${filename}.$image_ext") {
		print STDERR "-6: will write art image to (${filename}.$image_ext)\n"  if ($DEBUG);
		binmode IMGOUT;
		print IMGOUT $art_image;
		close IMGOUT;
		if (defined($tee2tmp) && $tee2tmp && open IMGOUT, ">${configPath}/${tee2tmp}.$image_ext") {
			binmode IMGOUT;
			print IMGOUT $art_image;
			close IMGOUT;
		}
		exit (0);
	}
}

sub writeTagData {   #FOR CDs:  WRITE ALL TAG-DATA FOUND FOR EACH TRACK TO user_tag_data AND, IF NEEDED, CUSTOM TAG-FILE (<disk-ID>.tag)
	my $comment = shift || '';
	my $tracktitles = shift || '';
	my $trackartists = shift || '';
	my @tagdata = ();
	my $trktitle = ($#{$tracktitles} >= 0) ? shift (@{$tracktitles}) : $title;
	my $trkartist = ($#{$trackartists} >= 0) ? shift (@{$trackartists}) : $artist;
	$trkartist = "Artist=$trkartist\n"  if ($trkartist =~ /\w/o);
	print STDERR "-7: comment=$comment= tagfid=${configPath}/tmp_tag_data=\n"  if ($DEBUG);
	if (open TAGDATA, "<${configPath}/tmp_tag_data") {  #MUST FIRST FETCH EXISTING user_tag_data SINCE WE'LL OVERWRITE IT:
		my $omit = 0;
		while (<TAGDATA>) {
			$omit = 1  if (/^\[cdda\:/);  #THIS ELIMINATES ANY EXISTING CD TAGS (WILL ALL BE REPLACED!):
			if ($omit) {                  #(CD TAGS ARE NOT TIED TO A SPECIFIC CD)
				$omit = 0  if (/^\[/o && !/cdda\:/);
			}
			push @tagdata, $_  unless ($omit);
		}
		close TAGDATA;
	}
	#NOW REPLACE user_tag_data APPENDING OUR CURRENT CD TAGS:
	if (open TAGDATA, ">${configPath}/tmp_tag_data") {
		while (@tagdata) {
			print TAGDATA shift(@tagdata);
		}
		# USER:CHANGE "DEFAULT" TO "OVERRIDE" BELOW TO OVERRIDE METADA FROM CDDA|B OR FROM CUSTOM TAG FILE:
		my $trk = 1;
		while (1) {
			print TAGDATA <<EOF;
[cdda://?$trk]
Precedence=DEFAULT
Title=$trktitle
${trkartist}Track=${trk}
$comment
EOF

			last  unless (@{$tracktitles});
			$trktitle = shift (@{$tracktitles});
			$trkartist = shift (@{$trackartists});
			$trkartist = "Artist=$trkartist\n"  if ($trkartist =~ /\w/o);
			++$trk;
		}
		close TAGDATA;
	}
}

sub albumart_done {
	my $omitlist = shift || '';;
	#CREATE TEMP. FILE TELLING LYRICWIKI PLUGIN WE'RE RUNNING (FIRST):
	if ($configPath && -d $configPath && open TMPFILE, ">${configPath}/_albumart_done.tmp") {
		print TMPFILE $omitlist;
		close TMPFILE;
	}
}

__END__
