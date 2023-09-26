#!/usr/bin/perl

#MUST INSTALL youtube-dl FOR Youtube to work!
#pp --gui -o FauxdaciousUrlHelper.exe -M urlhelper_mods.pm -M utf8_heavy.pl -l libeay32_.dll -l zlib1_.dll -l ssleay32_.dll FauxdaciousUrlHelper.pl

###(urlhelper_modules.pm contains):
###use StreamFinder;
###use StreamFinder::_Class;
###use StreamFinder::Anystream;
###use StreamFinder::Apple;
###use StreamFinder::Bitchute;
###use StreamFinder::Blogger;
###use StreamFinder::BrandNewTube;
###use StreamFinder::Brighteon;
###use StreamFinder::Castbox;
###use StreamFinder::Google;
###use StreamFinder::IHeartRadio;
###use StreamFinder::InternetRadio;
###use StreamFinder::LinkTV;
###use StreamFinder::Odysee;
###use StreamFinder::OnlineRadiobox;
###use StreamFinder::Podbean;
###use StreamFinder::PodcastAddict;
###use StreamFinder::RadioNet;
###use StreamFinder::Rcast;
###use StreamFinder::Rumble;
###use StreamFinder::SermonAudio;
###use StreamFinder::SoundCloud;
###use StreamFinder::Spreaker;
###use StreamFinder::Subsplash;
###use StreamFinder::Tunein;
###use StreamFinder::Vimeo;
###use StreamFinder::Youtube;
###use StreamFinder::Zeno;
###1;

#FAUXDACIOUS "HELPER" SCRIPT TO HANDLE URLS THAT FAUXDACIOUS CAN'T PLAY DIRECTLY:

#USAGE:  $0 URL [download-path]

#NOTE:  SEE ALSO THE CONFIG (.ini) FILE: FauxdaciousUrlHelper.ini in /contrib/, 
#       MOVE OVER TO ~/.config/fauxdacious[_instancename]/ AND EDIT TO SUIT YOUR NEEDS!

#CONFIGURE:  ~/.config/fauxdacious[_instancename]/config:  [audacious].url_helper=FauxdaciousUrlHelper.pl

#THIS SCRIPT WORKS BY TAKING THE URL THAT FAUXDACIOUS IS ATTEMPTING TO ADD TO IT'S
#PLAYLIST AND TRIES TO MATCH IT AGAINST USER-WRITTEN REGEX PATTERNS.  IF IT MATCHES
#ONE THEN CODE IS EXECUTED TO CONVERT IT TO A PLAYABLE URL AND WRITTEN OUT AS A 
#TEMPORARY ONE (OR MORE) LINE ".pls" "PLAYLIST" THAT FAUXDACIOUS WILL THEN ACTUALLY
#ADD TO IT'S PLAYLIST.  THIS IS NECESSARY BECAUSE THERE'S NO OTHER KNOWN WAY TO 
#CHANGE THE URL WITHIN FAUXDACIOUS.  IF THE URL "FALLS THROUGH" - NOT MATCHING ANY
#OF THE USER-SUPPLIED PATTERNS HERE, THIS PROGRAM EXITS SILENTLY AND FAUXACIOUS 
#HANDLES THE URL NORMALLY (UNCHANGED).
#THE CURRENTLY-PROVIDED PATTERNS ARE FOR "tunein.com" RADIO STATIONS AND "youtube" 
#VIDEOS, FOR WHICH THE ACTUALLY VALID STREAM URL MUST BE LOCATED AND RETURNED.
#TEMPLATE FOR ADDING USER-DEFINED PATTERNS:
#if ($ARGV[0] =~ <pattern>) {
#    <logic to locate proper stream URL, and perhaps the tag metadata / cover art url>
#    $newPlaylistURL = <proper stream URL to actually play>;
#    $title = <default descriptive title tag text to describe the stream>; #OPTIONAL
#    $comment = <additional tag metadata for tag file>; #OPTIONAL
#         #$comment SHOULD EITHER BE EMPTY OR IN THE FORM:  "Comment=string\nArtist=string...\n" etc.
#         #ONE USE FOR comment IS TO FETCH A COVER-ART THUMBNAIL IMAGE FILE AND PASS 
#         #IT'S URI/URL HERE AS "Comment=<art_uri>" SEE THE PREDEFINED PATTERNS FOR EXAMPLES:
#    &writeTagData($comment);
#}

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
use StreamFinder;
use URI::Escape;

#THESE SERVERS WILL TIMEOUT ON YOU TRYING TO STREAM, SO DOWNLOAD TO TEMP. FILE, THEN PLAY INSTEAD!:
#FORMAT:  '//www.problemserver.com' [, ...]
my @downloadServerList = ();  #USER MAY ADD ANY SUCH SERVERS TO THE LIST HERE OR IN FauxdaciousUrlHelper.ini.
my %setPrecedence = ();

die "..usage: $0 URL [download-path]\n"  unless ($ARGV[0]);
exit (0)  if ($ARGV[0] =~ m#^https?\:\/\/r\d+\-\-#);  #DON'T REFETCH FETCHED YOUTUBE PLAYABLE URLS!

my $configPath = '';
if ($ARGV[1]) {
	($configPath = $ARGV[1]) =~ s#^file:\/\/##;
}
my $newPlaylistURL = '';
my $title = $ARGV[0];
my $comment = '';
my $client = 0;
my $downloadit = 0;
my $DEBUG = defined($ENV{'FAUXDACIOUS_DEBUG'}) ? $ENV{'FAUXDACIOUS_DEBUG'} : 0;

	if ($configPath && open IN, "<${configPath}/FauxdaciousUrlHelper.ini") {
		while (<IN>) {
			s/\r//go;
			chomp;
			next  if (/^\#/o);
			next  unless (/\S/o);
			s/^\s+//o;
			s/\,$//o;
			my $key = (s/^(\w+?)\s*\=\s*//o) ? $1 : '';
			s/^[\'\"]//o;
			s/[\'\"]$//o;
			if ($key) {
				$setPrecedence{$_} = $key;
			} else {
				push @downloadServerList, $_;
			}
		}
		close IN;
	}

#BEGIN USER-DEFINED PATTERN-MATCHING CODE:

	if ($ARGV[0] !~ /\.(?:asx|mp3|mpv|m3u|m3u8|webm|pls|mov|mp[4acdp]|m4a|avi|flac|flv|og[agmvx]|wav|rtmp|3gp|a[ac]3|ape|dts|tta|mk[av])$/i) {  #ONLY FETCH STREAMS FOR URLS THAT DON'T ALREADY HAVE VALID EXTENSION!
		#SPECIAL HANDLING FOR rcast.net STREAMS (TOO SIMPLE TO NEED STREAMFINDER):
		if ($StreamFinder::VERSION < 2.04
				&& ($newPlaylistURL = $ARGV[0]) =~ s#https\:\/\/dir\.rcast\.net\/radio\/(\d+)\/?(.*)#https\:\/\/stream\.rcast\.net\/$1#) {
			$title = $2  if ($2);
			if ($title) {
				$title =~ s#\/$##;
				my @titleparts = split(/\-/, $title);
				for (my $i=0;$i<=$#titleparts;$i++) {
					$titleparts[$i] = "\u$titleparts[$i]\E";
				}
				$title = join(' ', @titleparts);
			} else {
				$title = $1;
			}
			$comment = 'Album=' . $ARGV[0] . "\n";
			goto GOTIT;
		}

		$client = new StreamFinder($ARGV[0], -debug => $DEBUG,
				-log => '/tmp/FauxdaciousUrlHelper.log',
				-logfmt => '[time] "[title]" - [site]: [url] ([total])'
		);
		die "f:Could not open streamfinder or no streams found!"  unless ($client);

		$newPlaylistURL = $client->getURL('-nopls');
		die "f:No streams for $ARGV[0]!"  unless ($newPlaylistURL);

		$title = $client->getTitle();
		$title =~ s/(?:\xe2?\x80\x99|\x{2019})/\'/g;  #*TRY* FIX SOME FUNKY QUOTES IN TITLE:
		$title =~ s/\\?u201[89]/\'/g;
		$title =~ s/\\?u201[cd]/\"/g;
		my $art_url = $client->getIconURL();
		my $desc = $client->getTitle('desc');

		#NOTE:  MOST PODCAST EPISODE mp3's SEEM TO COME WITH THEIR OWN EMBEDDED ICONS OR RELY ON THE ALBUM'S ICON:
		#(SO OPTIONS #1 & #2 ARE PBLY POINTLESS (CREATE USELESS REDUNDANT ICON FILES):
#1		(my $stationID = $client->getID()) =~ s#\/.*$##;  #0: THIS NAMES IMAGE AFTER STATION-ID.
		(my $stationID = $client->getID()) =~ s#\/#\_#;   #1: THIS NAMES IMAGE AFTER STATION-ID[_PODCAST-ID].
#2		my $stationID = $client->getID();                 #2: THIS KEEPS SEPARATE PODCAST-ID icon in SEPARATE SUBDIRECTORY, CREATING IF NEEDED:
#2		if ($stationID =~ m#^([^\/]+)\/#) {
#2			my $substationDIR = $1;
#2			`mkdir ${configPath}/${substationDIR}`  unless (-d "${configPath}/${substationDIR}");
#2		}

		if (defined($client->{album}) && $client->{album} =~ /\S/) {
			$comment = 'Album=' . (($client->{album} =~ /^https?\:/)
					? $client->{album} : ($client->{album}." - $ARGV[0]")) . "\n";
		} else {
			$comment = 'Album=' . $ARGV[0] . "\n";
		}
		$comment .= 'Artist='.$client->{artist}."\n"  if (defined($client->{artist}) && $client->{artist} =~ /\w/);
		$comment .= 'AlbumArtist='.$client->{albumartist}."\n"  if (defined($client->{albumartist}) && $client->{albumartist} =~ /\w/);
		$comment .= 'Year='.$client->{year}."\n"  if (defined($client->{year}) && $client->{year} =~ /\d\d\d\d/);
		$comment .= 'Genre='.$client->{genre}."\n"  if (defined($client->{genre}) && $client->{genre} =~ /\w/);
		#PUT DESCRIPTION FIELD, IF ANY INTO THE "Lyrics" FOR DISPLAY IN LYRICS PLUGINS.
		#NOTE:  IF THE STREAM HAS LYRICS IN id3 TAGS, THEY WILL REPLACE THIS, REGARDLESS OF "Precedence".
		#COMMENT OUT NEXT 7 LINES IF THIS IS NOT DESIRED:
		my $lyrics = '';
		if (defined($desc) && $desc =~ /\w/ && $desc ne $title) {
			$desc =~ s/\R+$//s;
			$desc =~ s/\R/\x02/gs; #MUST BE A SINGLE LINE, SO WORKAROUND FOR PRESERVING MULTILINE DESCRIPTIONS!
			$desc =~ s#(p|br)\>\s*\x02\s*\<(p|br)#$1\>\<$2#ig;  #TRY TO REMOVE SOME TRIPLE-SPACING.
			$lyrics = "Lyrics=Description:  $desc";
		}
		if ($art_url) {
			my ($image_ext, $art_image) = $client->getIconData;
			$image_ext =~ tr/A-Z/a-z/;
			if ($configPath && $art_image && open IMGOUT, ">${configPath}/${stationID}.$image_ext") {
				binmode IMGOUT;
				print IMGOUT $art_image;
				close IMGOUT;
				my $path = $configPath;
				if ($path =~ m#^\w\:#) { #WE'RE ON M$-WINDOWS, BUMMER: :(
					$path =~ s#^(\w)\:#\/$1\%3A#;
					$path =~ s#\\#\/#g;
				}
				if ($image_ext !~ /^(?:gif|jpg|jpeg|png)$/ && -x '/usr/bin/convert') {
					#GTK CAN'T HANDLE webp, com, ETC.!:
					`/usr/bin/convert '${path}/${stationID}.$image_ext' '${path}/${stationID}.png'`;
					if (-e "${path}/${stationID}.png") {
						unlink "${path}/${stationID}.$image_ext";
						$image_ext = 'png';
					}
				}
				$comment .= "Comment=file://${path}/${stationID}.$image_ext";
				my $art_url2 = $client->getIconURL('artist');
				if ($art_url2 && $art_url2 ne $art_url) {  #Handle some bad Apple in-podcast images
					my $site = $client->getType();
					sleep(5)  if ($site eq 'Rumble');  #THEY MAY INTERPRET 3RD HIT TOO SOON AS DOS & REJECT?
					my ($image_ext, $art_image) = $client->getIconData('artist');
					$image_ext =~ tr/A-Z/a-z/;
					if ($configPath && $art_image && length($art_image) > 499 #SANITY-CHECK (RUMBLE.COM)!
							&& open IMGOUT, ">${configPath}/${stationID}_channel.$image_ext") {
						binmode IMGOUT;
						print IMGOUT $art_image;
						close IMGOUT;
						my $path = $configPath;
						if ($path =~ m#^\w\:#) { #WE'RE ON M$-WINDOWS, BUMMER: :(
							$path =~ s#^(\w)\:#\/$1\%3A#;
							$path =~ s#\\#\/#g;
						}
						if ($image_ext !~ /^(?:gif|jpg|jpeg|png)$/ && -x '/usr/bin/convert') {
							#GTK CAN'T HANDLE webp, com, ETC.!:
							`/usr/bin/convert '${path}/${stationID}_channel.$image_ext' '${path}/${stationID}_channel.png'`;
							if (-e "${path}/${stationID}_channel.png") {
								unlink "${path}/${stationID}_channel.$image_ext";
								$image_ext = 'png';
							}
						}
						$comment .= ";file://${path}/${stationID}_channel.$image_ext";
					}
				}
				$comment .= "\n";
			}
		}
		if ($lyrics =~ /\w/) {  #PUT THIS LAST, AS IT SOMETIMES HAS GARBAGE THAT BLOCKS OTHER FIELDS.
			$lyrics =~ tr/\x00-\x01//d;  #ALSO TRY GETTING RID OF THE GARBAGE (BUT \x02 IS SPECIAL)!:
			$lyrics =~ tr/\x03-\x19//d;
			$lyrics =~ tr/\x7f-\xff//d;
			$lyrics =~ s/\\?u201[89]/\'/gs;
			$lyrics =~ s/\\?u201[cd]/\"/gs;
			$comment .= "$lyrics\n";
		}
		$comment =~ s/\0/ /gs;          #NO NULLS ALLOWED!
	} else {
		$newPlaylistURL = $ARGV[0];
	}

	foreach my $s (@downloadServerList) {
		if ($newPlaylistURL =~ /\Q$s\E/) {
			$downloadit = 1;
			$downloadit++  unless ($s =~ /^(https?\:)/o);
			last;
		}
	}
	if ($downloadit) {  #SOME SERVERS HANG UP IF TRYING TO STREAM, SO DOWNLOAD TO TEMP. FILE INSTEAD!:
		my $fn = $1  if ($newPlaylistURL =~ m#([^\/]+)$#);
		exit (0)  unless ($fn);
		$newPlaylistURL =~ s#^(\w+)\:#https:#  if ($downloadit > 1);
		unless (-f "/tmp/$fn") {
			my $cmd = "curl -o /tmp/$fn \"$newPlaylistURL\"";
			`$cmd`;
		}
		if (-f "/tmp/$fn") {   #TRY TO EXTRACT METADATA FROM THE DOWNLOADED FILE:
			$newPlaylistURL = "file:///tmp/$fn";
			my @tagdata = `ffprobe -loglevel error -show_entries format_tags -of default=noprint_wrappers=1 /tmp/$fn`;
			my $haveAlbum = 0;
			for (my $i=0;$i<=$#tagdata;$i++) {
				chomp $tagdata[$i];
				$tagdata[$i] =~ s/^TAG\:(\w)/\U$1\E/;
				my ($key, $val) = split(/\=/o, $tagdata[$i], 2);
				if ($key eq 'Title') {
					$title = $val;
				} elsif ($key eq 'Album') {
					$val .= ' - ' . $ARGV[0]  unless ($val =~ /^https?\:/);
					$comment .= $key . '=' . $val . "\n";
					$haveAlbum = 1;
				} elsif ($key =~ /^(?:Artist|Album|Genre|AlbumArtist|Comment|Year|Date)$/o) {
					$comment .= $tagdata[$i] . "\n";
				}
			}
			$comment =~ s/Date\=(\d\d\d\d[\r\n])/Year\=$1/s  unless ($comment =~ /\bYear\=/s);
			$comment .= "Album=$ARGV[0]\n"  unless ($haveAlbum);
		} else {
			exit (0);
		}
	}

	my $precedence = 'DEFAULT';
	foreach my $s (keys %setPrecedence) {   #ALLOW USER TO OVERRIDE TAG-SOURCE PRECEDENCE IN INI FILE:
		if ($ARGV[0] =~ /\Q$s\E/) {
			$precedence = "\U$setPrecedence{$s}\E";
			last;
		}
	}

GOTIT:
	$precedence = 'DEFAULT'  unless ($precedence =~ /^(?:OVERRIDE|ONLY|NONE)$/);
    &writeTagData($client, $precedence, $comment, $downloadit);

#END USER-DEFINED PATTERN-MATCHING CODE.

exit (0)  unless ($newPlaylistURL);

if ($configPath) {
	die "f:Can't open ${configPath}/tempurl.pls for writing ($!)"
			unless (open PLAYLISTOUT, ">${configPath}/tempurl.pls");
} else {
	open PLAYLISTOUT, ">&STDOUT";
}
print PLAYLISTOUT <<EOF;
[playlist]
NumberOfEntries=1
File1=$newPlaylistURL
EOF

close PLAYLISTOUT;

exit(0);

sub writeTagData {
	my $client = shift;
	my $precedence = shift;
	my $comment = shift || '';
	my $downloadit = shift || 0;
	my @tagdata = ();

	#Fauxd's g_key_file_get_string() CAN'T HANDLE HIGH-ASCII CHARS (ACCENTS, ETC.),
	#SO ESCAPE 'EM HERE, THEN UNESCAPE 'EM THERE!:
	$title = uri_escape($title, "\x80-\xff");
	$comment = uri_escape($comment, "\x80-\xff");
	# WE WRITE VIDEOS/PODCASTS TO A TEMP. TAG FILE, SINCE THEY EXPIRE AND ARE USUALLY ONE-OFFS, WHICH
	# WE THEREFORE WANT Fauxdacious TO DELETE THE TAGS AND COVER ART FILES WHEN PLAYLIST CLEARED (fauxdacious -D)!
	# THE LIST IN THE REGEX BELOW ARE THE ONES TO *NOT* DELETE ART IMAGES FOR (ie. STREAMING STATIONS)!:
	my $tagfid = (!$downloadit && $client && $client->getType()
			=~ /^(?:IHeartRadio|RadioNet|Tunein|InternetRadio|OnlineRadiobox|Rcast)$/)  #THESE SITES HAVE STATIONS:
			? 'user_tag_data' : 'tmp_tag_data';
	#WORKAROUND FOR IHEART & TUNEIN PODCASTS:
	if ($client && $tagfid =~ /^user/) {
		my $mediasource = $client->getType();
		$tagfid = 'tmp_tag_data'  if ($ARGV[0] =~ m#\/podcasts?\/#);  #CONSIDER ANY PODCASTS TO BE ONE-OFFS:
	}
	if (open TAGDATA, "<${configPath}/$tagfid") {
		my $omit = 0;
		while (<TAGDATA>) {
			$omit = 1  if (/^\[$newPlaylistURL\]/);
			if ($omit) {
				$omit = 0  if (/^\[/o && !/$newPlaylistURL/);
			}
			push @tagdata, $_  unless ($omit);
		}
		close TAGDATA;
	}
	if (open TAGDATA, ">${configPath}/$tagfid") {
		while (@tagdata) {
			print TAGDATA shift(@tagdata);
		}
		print TAGDATA <<EOF;
[$newPlaylistURL]
Precedence=$precedence
Title=$title
$comment
EOF

		close TAGDATA;
	}
}

__END__
