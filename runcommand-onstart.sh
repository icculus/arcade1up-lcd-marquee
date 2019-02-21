#/usr/bin/perl -w

# arcade1up-lcd-marquee; control an LCD in a Arcade1Up marquee.
#
# Please see the file LICENSE.txt in the source's root directory.
#
#  This file written by Ryan C. Gordon.

# (okay, despite the .sh filename, it's not _actually_ a shell script.)

use warnings;
use strict;
use XML::LibXML;

my $debug = 1;

sub usage {
    print STDERR "USAGE: $0 <SYSTEM> <EMULATOR> <ROM> <COMMAND>\n";
    exit(1);
}

my $system, $emulator, $rom, $cmd;
my $cmdline_ok = 0;
foreach (@ARGV) {
    $debug = 1, next if ($_ eq '--debug');
    $system = $_, next if (not defined $host);
    $emulator = $_, next if (not defined $emulator);
    $rom  = $_, next if (not defined $rom);
    $cmd = $_, $cmdline_ok = 1, next if (not defined $cmd);
    usage();
}

usage() if not $cmdline_ok;

print("Starting up!\n") if $debug;

if ($debug) {
    print("system: '$system'\n");
    print("emulator: '$emulator'\n");
    print("rom: '$rom'");
    print("command: '$cmd'\n");
}


# Okay, so we're going to dig into the system's game list that
#  EmulationStation uses, and see what metadata is available for it. We'll
#  try to find a useful image in there. We find the game by looking for one
#  with the same "rom" specified. It's not a perfect system, but it'll do.

# !!! FIXME: this works for emulators but not "ports" like sdlpop.
my $sysdir = "/home/pi/RetroPie/roms/$system";
my $gamelist = "$sysdir/gamelist.xml";

exit(0) if ( ! -f $gamelist );  # nothing to do...

$rom = `realpath "$rom"`;

my $xml = XML::LibXML->load_xml(location => $gamelist);
foreach my $game ($dom->findnodes('/gameList/game')) {
    my $path = $game->findvalue('./path');
    next if not $path;
    if (not $path =~ /\A\//) {
        $path = `realpath "$sysdir/$path"`;
    }

    next if $path ne $rom;

    my $name = $game->findvalue('./name');
    $name = '[title unknown]' if not $name;
    print("Found it! It appears to be \"$name\"\n") if $debug;

    my $img = $game->findvalue('./marquee');
    $img = $game->findvalue('./image') if not $img;

    if ($img) {
        if (not $img =~ /\A\//) {
            $img = `realpath "$sysdir/$img"`;
        }
    }

    if (not $img) {
        # !!! FIXME: maybe a system image (like an Apple for basilisk?) if still not found?
        print("Found game, but no usable image for it.  :(\n") if $debug;
    } else {
        print("Going with image '$img' for the marquee!\n") if $debug;
        system("/home/pi/projects/arcade1up-lcd-marquee/marquee-showimage $img");
    }

    last;
}

print("All done!\n") if $debug;

exit(0);

# end of runcommand_onstart.sh ...

