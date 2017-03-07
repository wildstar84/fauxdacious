#!/usr/bin/perl

#	youtube-dl METADATA HELPER SCRIPT
#	by (c:2016) Jim Turner <turnerjw784@yahoo.com>
#   licenced under the same license as Fauxdacious itself.
#
#	This program fetches the title and artist metadata from youtube videos without actually downloading.
#	It appends it to the file:  ~/.config/audacious/youtubedl_tag_data which Fauxdacious then reads back 
#	in to set the playlist and video window titles before the video begins to download and play.
#	To use this script, copy it to /usr/local/bin (or anywhere) and edit ~/.config/audacious/config and 
#	set:  [audacious].youtubedl_tag_data=TRUE,	
#	and [youtubedl].metadata_helper=/usr/local/bin/youtubedl_metadatahelper.pl (or whereever you put it).
#	NOTE:  You'll need to install the Perl module:  WWW::YouTube::Download from http://www.cpan.org.
#	Without this helper app. Fauxdacious will display the video's URL as the title.

use LWP::Simple;
use WWW::YouTube::Download;

my $client = WWW::YouTube::Download->new;
(my $realURL = $ARGV[0]) =~ s/^ytdl\:/https\:/;
(my $realOutFile = $ARGV[1]) =~ s#^file:\/\/##;
open LOG, ">/tmp/xyoutubedl_metadatahelper.log";
print LOG "-realurl=$realURL=\n";
print LOG "-realfile=$realOutFile=\n";
my $meta_data = $client->prepare_download($realURL);

my %metadata;
foreach my $name (qw(video_id video_url title user fmt fmt_list suffix)) {
	#eval "print \"--metadata($name)=\".\$client->get_$name(\$ARGV[0]).\"=\\n\";";
	eval "\$metadata{$name} = \$client->get_$name(\$realURL);";
}

if (open OUT, ">>$realOutFile") {
	print OUT <<END;
[$ARGV[0]]
Precedence=DEFAULT
Title=$metadata{title}
Artist=$metadata{user}
END

	close OUT;
	my $art_url = `/usr/local/bin/youtube-dl --get-thumbnail $realURL`;
	my $art_image = $art_url ? LWP::Simple::get($art_url) : '';
	if ($art_image) {
		(my $image_ext = $art_url) =~ s/^.+\.//;
		if (open IMGOUT, ">/tmp/$metadata{video_id}.$image_ext") {
			print IMGOUT $art_image;
			close IMGOUT;
			open OUT, ">>$realOutFile";
			print OUT "Comment=file:///tmp/$metadata{video_id}.$image_ext\n";
			close OUT;
		}
	}
	print OUT "\n";
	close OUT;
}

close LOG;

exit(0);
