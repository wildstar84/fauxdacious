#!/usr/bin/perl

#MUST INSTALL youtube-dl FOR Youtube to work!

#FAUXDACIOUS "HELPER" SCRIPT TO HANDLE URLS THAT FAUXDACIOUS CAN'T PLAY DIRECTLY:
#THIS SCRIPT WORKS BY TAKING THE URL THAT FAUXDACIOUS IS ATTEMPTING TO ADD TO IT'S
#PLAYLIST AND TRIES TO MATCH IT AGAINST USER-WRITTEN REGEX PATTERNS.  IF IT MATCHES
#ONE THEN CODE IS EXECUTED TO CONVERT IT TO A PLAYABLE URL AND WRITTEN OUT AS A 
#TEMPORARY ONE (OR MORE) LINE ".pls" "PLAYLIST" THAT FAUXDACIOUS WILL THEN ACTUALLY
#ADD TO IT'S PLAYLIST.  THIS IS NECESSARY BECAUSE THERE'S NO OTHER KNOWN WAY TO 
#CHANGE THE URL WITHING FAUXDACIOUS.  IF THE URL "FALLS THROUGH" - NOT MATCHING ANY
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

use strict;
use LWP::Simple qw();
use Tunein::Streams;

die "..usage: $0 url\n"  unless ($ARGV[0]);
my $configPath = '';
if ($ARGV[1]) {
	($configPath = $ARGV[1]) =~ s#^file:\/\/##;
}
my $newPlaylistURL = '';
my $title = $ARGV[0];

#BEGIN USER-DEFINED PATTERN-MATCHING CODE:
if ($ARGV[0] =~ m#\:\/\/tunein\.com\/#) {   #HANDLE tunein.com STATION URLS:
	my $station = new Tunein::Streams($ARGV[0]);
	die "f:Invalid URL($ARGV[0]) or no streams!\n"  unless ($station);
	$newPlaylistURL = $station->getBest('Url');
	die "f:No streams for $ARGV[0]!\n"  unless ($newPlaylistURL);
	
	my $art_url = $station->getIconURL();
	my $stationID = $station->getStationID();
	$title = $station->getStationTitle();
	my $comment = '';
	if ($art_url) {
		my $art_image = $art_url ? LWP::Simple::get($art_url) : '';
		(my $image_ext = $art_url) =~ s/^.+\.//;
		if ($configPath && $art_image && open IMGOUT, ">${configPath}/${stationID}.$image_ext") {
			print IMGOUT $art_image;
			close IMGOUT;
			$comment = "Comment=file://${configPath}/${stationID}.$image_ext\n";
		}
	}
	&writeTagData($comment);
} elsif ($ARGV[0] =~ m#^http[s]?\:\/\/\w*\.*youtube\.#) {   #HANDLE Youtube URLS:
	my $client = WWW::YouTube::Download->new;
	my $meta_data = $client->prepare_download($ARGV[0]);
	my %metadata;
	foreach my $name (qw(video_id title user)) {
		eval "\$metadata{$name} = \$client->get_$name(\$ARGV[0]);";
	}
	$title = $metadata{title};
	my $comment = "Album=$ARGV[0]\n";
	$comment .= "Artist=$metadata{user}\n"  if ($metadata{user});
	$_ = `youtube-dl --get-url --get-thumbnail -f mp4 $ARGV[0]`;
	my @urls = split(/\r?\n/);
	$newPlaylistURL = $urls[0];
	my $art_url = ($#urls >= 1) ? $urls[$#urls] : '';
	if ($configPath) {
		my $art_image = $art_url ? LWP::Simple::get($art_url) : '';
		if ($art_image) {
			(my $image_ext = $art_url) =~ s/^.+\.//;
			if (open IMGOUT, ">/tmp/$metadata{video_id}.$image_ext") {
				print IMGOUT $art_image;
				close IMGOUT;
				$comment .= "Comment=file:///tmp/$metadata{video_id}.$image_ext\n";
			}
		}
	}
	chomp $newPlaylistURL;
	die "f:No valid video stream found!\n"  unless ($newPlaylistURL);
	&writeTagData($comment);
}
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
	my $comment = shift || '';
	my @tagdata = ();
	if (open TAGDATA, "<${configPath}/user_tag_data") {
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
	if (open TAGDATA, ">${configPath}/user_tag_data") {
		while (@tagdata) {
			print TAGDATA shift(@tagdata);
		}
		print TAGDATA <<EOF;
[$newPlaylistURL]
Precedence=DEFAULT
Title=$title
$comment
EOF

		close TAGDATA;
	}
}


__END__
