#!/usr/bin/perl

#Fauxdacious helper script for fetching the actual streams and cover art thumbnail images for 
#tunein.com radio station URLs via Tunein::Stream.
#Copyright 2017 by Jim Turner under the GPL.

#   to compile into exe via par:  pp -o getTuneinStream.exe getTuneinStream.pl
#   then move exe to same location as Fauxdacious (/aud/bin/) or somewhere in your exe path.

	use strict;
	use LWP::Simple qw();
	use Tunein::Streams;

	die "..usage: $0 url\n"  unless ($ARGV[0]);

	my $station = new Tunein::Streams($ARGV[0]);
	my $configPath = '';
	if ($ARGV[1]) {
		($configPath = $ARGV[1]) =~ s#^file:\/\/##;
	}

	die "f:Invalid URL($ARGV[0]) or no streams!\n"  unless ($station);
	my $best = $station->getBest('Url');
	die "f:No streams for $ARGV[0]!\n"  unless ($best);

	print <<EOF;
[playlist]
NumberOfEntries=1
File1=$best
EOF

	(my $bestshort = $best) =~ s/\?.*$//;
	my $art_url = $station->getIconURL();
	if ($configPath) {
		my $stationID = $station->getStationID();
		my $title = $station->getStationTitle();
		my $art_image = $art_url ? LWP::Simple::get($art_url) : '';
		(my $image_ext = $art_url) =~ s/^.+\.//;
		if ($art_image && open IMGOUT, ">${configPath}/${stationID}.$image_ext") {
			binmode IMGOUT;
			print IMGOUT $art_image;
			close IMGOUT;
		}
		my @tagdata = ();
		if (open TAGDATA, "<${configPath}/user_tag_data") {
			my $omit = 0;
			while (<TAGDATA>) {
				$omit = 1  if (/^\[$bestshort/);
				if ($omit) {
					$omit = 0  if (/^\[/o && !/$bestshort/);
				}
				push @tagdata, $_  unless ($omit);
			}
			close TAGDATA;
		}
		if (open TAGDATA, ">${configPath}/user_tag_data") {
			while (@tagdata) {
				print TAGDATA shift(@tagdata);
			}
			if ($configPath =~ m#^\w\:#) {  #WE'RE ON M$-WINDOWS, BUMMER: :(
				$configPath =~ s#^(\w)\:#\/$1\%3A#;
				$configPath =~ s#\\#\/#g;
			}
			print TAGDATA <<EOF;

[$best]
Precedence=DEFAULT
Title=$title
Comment=file://${configPath}/${stationID}.$image_ext

EOF

			close TAGDATA;
		}
	}

exit(0);

__END__
