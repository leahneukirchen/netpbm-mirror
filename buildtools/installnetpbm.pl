#!/usr/bin/perl -w

require 5.000;

#Note: mkdir() must have 2 arguments as late as 5.005.

use strict;
use English;
use Fcntl;
use File::Basename;
use Cwd qw(getcwd);

my ($TRUE, $FALSE) = (1,0);

my $cpCommand;
#use vars qw($cpCommand);

#******************************************************************************
#
#  SUBROUTINES
#
#*****************************************************************************

sub autoFlushStdout() {
    my $oldFh = select(STDOUT); 
    $OUTPUT_AUTOFLUSH = $TRUE;
    select($oldFh);
}



sub prompt($$) {

    my ($prompt, $default) = @_;

    print("$prompt ($default) ==> ");

    my $response = <STDIN>;

    chomp($response);
    if ($response eq "") {
        $response = $default;
    }

    return $response;
}



sub fsObjPrompt($$) {
    my ($prompt, $default) = @_;
#-----------------------------------------------------------------------------
#  Prompt for a filesystem object name and accept glob pattern such as
#  ~/mydir and /usr/lib/net* .
#
#  If there are zero or multiple filesystem object names that match the
#  pattern the user gave, ask again.  If there is only one possible name
#  consistent with the user's response, return that even if no filesystem
#  object by that name exists.
#-----------------------------------------------------------------------------
    my $globbedResponse;

    while (!$globbedResponse) {
        my $response = prompt($prompt, $default);

        my @matchList = glob($response);

        if (@matchList == 0) {
            print("No filesystem object matches that pattern\n");
        } elsif (@matchList > 1) {
            print("Multiple filesystem objects match that pattern\n");
        } else {
            $globbedResponse = $matchList[0];
        }
    }
    return $globbedResponse;
}



sub getPkgdir() {
#-----------------------------------------------------------------------------
#  Find out from the user where the Netpbm package is (i.e. where
#  'make package' put it).
#-----------------------------------------------------------------------------
    my $pkgdir;

    # We allow the user to respond with a shell filename pattern.  This seems
    # like a lot of complexity for a barely useful feature, but we actually
    # saw a problem where a user typed ~/mypackage without realizing that ~ is
    # a globbing thing and was stumped when we said no such file exists, while
    # shell commands said it does.

    # Note that glob() of something that has no wildcard/substitution
    # characters just returns its argument, whether a filesystem object by
    # that name exists or not.  But for a wildcard pattern that doesn't match
    # any existing files, glob() returns an empty list.

    while (!$pkgdir) {
    
        print("Where is the install package you created with " .
              "'make package'?\n");
        my $default = "/tmp/netpbm";
        
        my $response = prompt("package directory", $default);

        my @matchList = glob($response);

        if (@matchList == 0) {
            print("No filesystem object matches that pattern\n");
        } elsif (@matchList > 1) {
            print("Multiple filesystem objects match that pattern\n");
        } else {
            my $fsObjNm = $matchList[0];
            
            if (!-e($fsObjNm)) {
                print("No filesystem object named '$fsObjNm' exists.\n");
            } else {
                if (!-d($fsObjNm)) {
                    print("'$fsObjNm' is not a directory\n");
                } else {
                    if (!-f("$fsObjNm/pkginfo")) {
                        print("Directory '$fsObjNm' does not appear to be " .
                              "a Netpbm install package. \n");
                        print("It does not contain a file named 'pkginfo'.\n");
                    } else {
                        $pkgdir = $fsObjNm;
                    }
                }
            }
        }
        print("\n");
    }
    print("\n");
    return $pkgdir;
}



sub makePrefixDirectory($) {

    my ($prefixDir) = @_;

    if ($prefixDir ne "" and !-d($prefixDir)) {
        print("No directory named '$prefixDir' exists.  " .
              "Do you want to create it?\n");

        my $done;
        while (!$done) {
            my $response = prompt("Y(es) or N(o)", "Y");
            if (uc($response) eq "Y") {
                my $success = mkdir($prefixDir, 0777);
                if (!$success) {
                print("Unable to create directory '$prefixDir'.  " .
                      "Error is $ERRNO\n");
            }
                $done = $TRUE;
            } elsif (uc($response) eq "N") {
                $done = $TRUE;
            } 
        }
        print("\n");
    }
}





sub getPrefix() {

    print("Enter the default prefix for installation locations.  " .
          "I will use \n");
    print("this in generating defaults for the following prompts to " .
          "save you \n");
    print("typing.  If you plan to spread Netpbm across your system, \n" .
          "enter '/'.\n");
    print("\n");

    my $default;
    if ($OSNAME eq "cygwin") {
        $default = "/usr/local";
    } elsif ($ENV{OSTYPE} && $ENV{OSTYPE} eq "msdosdjgpp") {
        $default = "/dev/env/DJDIR";
    } else {
        $default = "/usr/local/netpbm";
    }

    my $response = fsObjPrompt("install prefix", $default);

    my $prefix;

    # Remove possible trailing /
    if (substr($response,-1,1) eq "/") {
        $prefix = substr($response, 0, -1);
    } else {
        $prefix = $response;
    }
    print("\n");

    makePrefixDirectory($prefix);

    return $prefix;
}



sub getCpCommand() {
#-----------------------------------------------------------------------------
# Compute the command + options need to do a recursive copy, preserving
# symbolic links and file attributes.
#-----------------------------------------------------------------------------
    my $cpCommand;

    # We definitely need more intelligence here, but we'll need input from
    # users to do it.  Maybe we should just bundle GNU Cp with Netpbm as an
    # install tool.  Maybe we should write a small recursive copy program
    # that uses more invariant tools, like buildtools/install.sh does for
    # single files.

    if (`cp --version 2>/dev/null` =~ m/GNU/) {
        # It's GNU Cp -- we have options galore, and they're readable.
        $cpCommand = "cp --recursive --preserve --no-dereference";
    } else {
        # This works on Cp from "4th Berkeley Distribution", July 1994.
        # Mac OSX has this.
        # -R means recursive with no dereferencing of symlinks
        # -p means preserve attributes
        $cpCommand = "cp -R -p";
    }
    return($cpCommand);
}



sub getBinDir($) {
#-----------------------------------------------------------------------------
#  Find out from the user where he wants the programs installed, and return
#  that.
#-----------------------------------------------------------------------------
    my ($prefix) = @_;

    print("Where do you want the programs installed?\n");
    print("\n");

    my $binDir;

    while (!$binDir) {
        my $default = "$prefix/bin";

        my $response = fsObjPrompt("program directory", $default);
        
        if (-d($response)) {
            $binDir = $response;
        } else {
            my $succeeded = mkdir($response, 0777);
            
            if (!$succeeded) {
                print("Unable to create directory '$response'.  " .
                      "Error=$ERRNO\n");
            } else {
                $binDir = $response;
            }
        }
    }
    print("\n");

    return $binDir;
}



sub installProgram($$$) {

    my ($pkgdir, $prefix, $bindirR) = @_;

    my $binDir = getBinDir($prefix);

    print("Installing programs...\n");

    my $rc = system("$cpCommand $pkgdir/bin/* $binDir/");

    if ($rc != 0) {
        print("Copy of programs from $pkgdir/bin to $binDir failed.\n");
        print("cp return code is $rc\n");
    } else {
        print("Done.\n");
    }
    $$bindirR = $binDir;
}



sub getLibDir($) {
#-----------------------------------------------------------------------------
#  Find out from the user where he wants the runtime libraries installed and
#  return that.
#-----------------------------------------------------------------------------
    my ($prefix) = @_;

    print("Where do you want the shared library installed?\n");
    print("\n");

    my $libDir;

    while (!$libDir) {
        my $default = "$prefix/lib";

        my $response = fsObjPrompt("shared library directory", $default);
        
        if (-d($response)) {
            $libDir = $response;
        } else {
            my $succeeded = mkdir($response, 0777);
            
            if (!$succeeded) {
                print("Unable to create directory '$response'.  " .
                      "Error=$ERRNO\n");
            } else {
                $libDir = $response;
            }
        }
    }
    print("\n");

    return $libDir;
}



sub ldconfigExists() {

    return (system("ldconfig -? >/dev/null 2>/dev/null") >> 8) != 127;
}



sub crleExists() {

    return (system("crle -? 2>/dev/null") >> 8) != 127;
}



sub dirName($) {
    my ($fileName) = @_;
#-----------------------------------------------------------------------------
#  The directory component of file name $fileName.
#-----------------------------------------------------------------------------

    my @components = split(m{/}, $fileName);

    pop(@components);

    if (@components == 1 && $components[0] eq '') {
        return '/';
    } else {
        return join('/', @components);
    }
}



sub ldConfigKnowsDir($) {
    my ($shlibDir) = @_;
#-----------------------------------------------------------------------------
#  Ldconfig appears to search $shlibDir for shared libraries.
#
#  Our determination is approximate.  We just look at whether 'ldconfig'
#  found anything in $shlibDir the last time it searched.  If it searched
#  $shlibDir and just didn't find anything or $shlibDir has been added to
#  its search path since then, we'll wrongly conclue that it doesn't search
#  $shlibDir now.
#-----------------------------------------------------------------------------
    my @ldconfigOutput = split(m{\n}, qx{ldconfig -p});

    my $found;

    foreach (@ldconfigOutput) {

        if (m{ => \s (.*) $ }x) {
            my ($fileName) = ($1);

            if (dirName($fileName) eq $shlibDir) {
                $found = $TRUE;
            }
        }
    }
    return $found;
}




sub warnNonstandardShlibDirLdconfig($) {
    my ($shlibDir) = @_;
#-----------------------------------------------------------------------------
#  Assuming this is a system that has an 'ldconfig' program, warn the user
#  if $shlibDir appears not to be in the system shared library search path.
#-----------------------------------------------------------------------------

    # This appears to be a system that uses the GNU libc dynamic linker.
    # The list of system shared library directories is in /etc/ld.so.conf.
    # The program Ldconfig searches the directories in that list and
    # remembers all the shared libraries it found (and some informtaion
    # about them) in its cache /etc/ld.so.cache, which is what the 
    # dynamic linker uses at run time to find the shared libraries.

    if (!ldConfigKnowsDir($shlibDir)) {
        print("You have installed shared libraries in " .
              "'$shlibDir',\n" .
              "which does not appear to be a system shared " .
              "library directory ('ldconfig -p' \n" .
              "doesn't show any other libraries in there).  " .
              "Therefore, the system may not be\n" .
              "able to find the Netpbm shared libraries " .
              "when you run Netpbm programs.\n" .
              "\n" .
              "To fix this, you may need to update /etc/ld.so.conf\n" .
              "\n" .
              "You may need to use an LD_LIBRARY_PATH " .
              "environment variable when running Netpbm programs\n" .
              "\n");
    }
}




sub warnNonstandardShlibDirCrle($) {
    my ($shlibDir) = @_;
#-----------------------------------------------------------------------------
#  Assuming this is a system that has a 'crle' program, warn the user
#  if $shlibDir appears not to be in the system shared library search path.
#-----------------------------------------------------------------------------
    # We should use 'crle' here to determine whether $shlibDir is a
    # system directory.  But I don't have a Solaris system to reverse
    # engineer/test with.

    if ($shlibDir ne "/lib" && $shlibDir ne "/usr/lib") {
        print("You have installed shared libraries in " .
              "'$shlibDir',\n" .
              "which is not a conventional system shared " .
              "library directory.\n" .
              "Therefore, the system may not be able to " .
              "find the Netpbm\n" .
              "shared libraries when you run Netpbm programs.\n" .
              "\n" .
              "To fix this, you may need to run 'crle -l'.\n" .
              "\n" .
              "You may need to use an LD_LIBRARY_PATH " .
              "environment variable when running Netpbm programs\n" .
              "\n");
    }
}
        


sub warnNonstandardShlibDirGeneric($) {
    my ($shlibDir) = @_;
#-----------------------------------------------------------------------------
#  Without assuming any particular shared library search scheme on this
#  system, warn if $shlibDir appears not to be in the system shared library
#  search path.
#-----------------------------------------------------------------------------

    if ($shlibDir ne "/lib" && $shlibDir ne "/usr/lib") {
        print("You have installed shared libraries in " .
              "'$shlibDir',\n" .
              "which is not a conventional system shared " .
              "library directory.\n" .
              "Therefore, the system may not be able to " .
              "find the Netpbm\n" .
              "shared libraries when you run Netpbm programs.\n" .
              "\n" .
              "You may need to use an LD_LIBRARY_PATH " .
              "environment variable when running Netpbm programs\n" .
              "\n");
    }
}



sub warnNonstandardShlibDir($) {
    my ($shlibDir) = @_;

    if (ldconfigExists()) {
        warnNonstandardShlibDirLdconfig($shlibDir);
    } elsif (crleExists()) {
        warnNonstandardShlibDirCrle($shlibDir);
    } else {
        warnNonstandardShlibDirGeneric($shlibDir);
    }
}



sub execLdconfig() {
#-----------------------------------------------------------------------------
#  Run Ldconfig.  Try with the -X option first, and if that is an invalid
#  option (which we have seen on an openBSD system), try it without -X.
#
#  -X means "don't create any symlinks."  Any symlinks required should be
#  created as part of installing the library, so we don't need that function
#  from Ldconfig.  And we want to tread as lightly as possible on the 
#  system -- we don't want creating symlinks that have nothing to do with
#  Netpbm to be a hidden side effect of installing Netpbm.
#
#  Note that this Ldconfig works only if the user installed the Netpbm
#  library in a standard directory that Ldconfig searches.  Note that on
#  OpenBSD, Ldconfig is hardcoded to search only /usr/lib ever.  We could
#  also do 'ldconfig DIR' to scan the particular directory in which we
#  installed the Netpbm library.  But 1) the effects of this would disappear
#  the next time the user rebuilds the cache file; and 2) on OpenBSD, this
#  causes the cache file to be rebuilt from ONLY that directory.  On OpenBSD,
#  you can add the -m option to cause it to ADD the contents of DIR to the
#  existing cache file.
#  
#-----------------------------------------------------------------------------
# Implementation note:  We've seen varying completion codes and varying
# error messages from different versions of Ldconfig when it fails.

    my $ldconfigSucceeded;

    my $ldconfigXResp = `ldconfig -X 2>&1`;

    if (!defined($ldconfigXResp)) {
        print("Unable to run Ldconfig.\n");
        $ldconfigSucceeded = $FALSE;
    } elsif ($ldconfigXResp eq "") {
        $ldconfigSucceeded = $TRUE;
    } elsif ($ldconfigXResp =~ m{usage}i) {
        print("Trying Ldconfig again without the -X option...\n");

        my $rc = system("ldconfig");
        
        $ldconfigSucceeded = ($rc == 0);
    } else {
        print($ldconfigXResp);
        $ldconfigSucceeded = $FALSE;
    }
    
    if ($ldconfigSucceeded) {
        print("Ldconfig completed successfully.\n");
    } else {
        print("Ldconfig failed.  You will have to fix this later.\n");
    }
}



sub doLdconfig() {
#-----------------------------------------------------------------------------
#  Run Ldconfig where appropriate.
#-----------------------------------------------------------------------------
    if ($OSNAME eq "linux" || ldconfigExists()) {
        # This is a system where Ldconfig makes sense

        print("In order for the Netpbm shared library to be found when " .
              "you invoke \n");
        print("A Netpbm program, you must either set an environment " .
              "variable to \n");
        print("tell where to look for it, or you must put its location " .
              "in the shared \n");
        print("library location cache.  Do you want to run Ldconfig now " .
              "to put the \n");
        print("Netpbm shared library in the cache?  This works only if " .
              "you have\n");
        print("installed the library in a directory Ldconfig knows about.\n");
        print("\n");
        
        my $done;

        $done = $FALSE;

        while (!$done) {
            my $response = prompt("Y(es) or N(o)", "Y");

            if (uc($response) eq "Y") {
                execLdconfig();
                $done = $TRUE;
            } elsif (uc($response) eq "N") {
                $done = $TRUE;
            } else {
                print("Invalid response.  Enter 'Y' or 'N'\n");
            }
        }
    }
}



sub installSharedLib($$$) {

    my ($pkgdir, $prefix, $libdirR) = @_;

    if (-d("$pkgdir/lib")) {
        my $libDir = getLibDir($prefix);

        print("Installing shared libraries...\n");

        my $rc = system("$cpCommand $pkgdir/lib/* $libDir/");

        if ($rc != 0) {
            print("Copy of libraries from $pkgdir/lib to $libDir failed.\n");
            print("cp return code is $rc\n");
        } else {
            print("done.\n");
            print("\n");

            warnNonstandardShlibDir($libDir);

            doLdconfig();
        }
        $$libdirR = $libDir;
    } else {
        print("You did not build a shared library, so I will not " .
              "install one.\n");
    }
    print("\n");
}



sub getSharedLinkDir($) {
#-----------------------------------------------------------------------------
#  Find out from the user where he wants the shared library stubs installed
#  and return that.
#-----------------------------------------------------------------------------
    my ($prefix) = @_;

    print("Where do you want the shared library stub (used to link-edit\n" .
          "programs to use the shared lirary) installed?\n");
    print("\n");

    my $linkDir;

    while (!$linkDir) {
        my $default = "$prefix/lib";

        my $response = fsObjPrompt("shared library stub directory", $default);
        
        if (-d($response)) {
            $linkDir = $response;
        } else {
            my $succeeded = mkdir($response, 0777);
            
            if (!$succeeded) {
                print("Unable to create directory '$response'.  " .
                      "Error=$ERRNO\n");
            } else {
                $linkDir = $response;
            }
        }
    }
    print("\n");

    return $linkDir;
}



sub removeDotDirs($) {

    my ($readDirResultR) = @_;

    my @dirContents;

    foreach (@{$readDirResultR}) {
        if ($_ ne '.' && $_ ne '..') {
            push(@dirContents, $_);
        }
    }

    return \@dirContents;
}



sub readDirContents($$$) {
    my ($dirName, $contentsRR, $errorR) = @_;
#-----------------------------------------------------------------------------
#  Return the contents of the directory named $dirName, excluding the
#  fake . and .. entries.
#-----------------------------------------------------------------------------
    my $dirContentsR;
    my $error;

    my $success = opendir(DIR, $dirName);

    if (!$success) {
        $error = "Unable to open directory '$dirName' with opendir()";
    } else {
        my @readDirResult = readdir(DIR);

        $dirContentsR = removeDotDirs(\@readDirResult);

        closedir(DIR);
    }

    $$contentsRR = $dirContentsR;

    if ($errorR) {
        $$errorR = $error;
    }
}



sub dirContents($) {
    my ($dirName) = @_;
#-----------------------------------------------------------------------------
#  Return the contents of the directory named $dirName, excluding the
#  fake . and .. entries.
#-----------------------------------------------------------------------------

    readDirContents($dirName, \my $contentsR, \my $error);

    if ($error) {
        die($error);
    }
    return @{$contentsR};
}



sub fixSharedStubSymlink($$) {
#-----------------------------------------------------------------------------
#  This is a hack to install a shared library link on a GNU system.
#
# On systems that use the GNU dynamic linker, the shared library stub (the
# file one uses at link-edit time to tell the linker what it needs to know
# about the shared library that the code will use at run time) is just a
# symbolic link to a copy of the actual shared library.  In the Netpbm
# package, this is a relative symbolic link to the shared library in the
# package.

# Assuming Caller just copied the contents of the 'sharedlink' directory
# straight from the package to the install target system, that symbolic link
# isn't necessarily correct, and even if it is, it's probably messy.  (In the
# normal case, the link value is ../lib/libnetpbm.so.<MAJ>).

# So what we do is just detect and patch up that case.  If the stub is a
# symbolic link to something in the shared library directory of the package,
# we replace it with a symbolic link to the same thing in the shared library
# directory of the install target system.
# -----------------------------------------------------------------------------
    my ($linkDir, $shlibDir) = @_;

    my $oldCwd = getcwd();
    chdir($linkDir);

    foreach my $fsObjNm (dirContents('.')) {
        if (-l("$fsObjNm")) {
            if (readlink($fsObjNm) =~ m{^\.\./lib/(.*)$}) {
                my $shlibNm = $1;

                unlink($fsObjNm) or
                    die("Failed to delete symlink copied from package " .
                        "in order to replace it with a proper symlink " .
                        "for this installation");

                if ($linkDir eq $shlibDir) {
                    symlink($shlibNm, $fsObjNm) or
                        die("Failed to create symlink as shared library stub");
                } else {
                    symlink("$shlibDir/$shlibNm", $fsObjNm) or
                        die("Failed to create symlink as shared library stub");
                }
                    
                print("Linked $shlibDir/$shlibNm from $linkDir/$fsObjNm");
            }
        }
    }
    chdir($oldCwd);
}



sub installSharedStub($$$$) {

    my ($pkgdir, $prefix, $shlibDir, $linkdirR) = @_;

    if (-d("$pkgdir/sharedlink")) {
        my $linkDir = getSharedLinkDir($prefix);

        print("Installing shared library stubs.\n");

        my $rc = system("$cpCommand $pkgdir/sharedlink/* $linkDir/");

        if ($rc != 0) {
            print("Copy of files from $pkgdir/sharedlink " .
                  "to $linkDir failed.\n");
            print("cp return code is $rc\n");
        } else {
            fixSharedStubSymlink($linkDir, $shlibDir);

            print("done.\n");
        }
        $$linkdirR = $linkDir;
    } else {
        print("You did not build a shared library, so I will not " .
              "install a stub \n");
        $$linkdirR = undef;
    }
}



sub getStaticLinkDir($) {
#-----------------------------------------------------------------------------
#  Find out from the user where he wants the static  libraries installed and
#  return that.
#-----------------------------------------------------------------------------
    my ($prefix) = @_;

    print("Where do you want the static link library installed?\n");
    print("\n");

    my $linkDir;

    while (!$linkDir) {
        my $default = "$prefix/lib";

        my $response = fsObjPrompt("static library directory", $default);
        
        if (-d($response)) {
            $linkDir = $response;
        } else {
            my $succeeded = mkdir($response, 0777);
            
            if (!$succeeded) {
                print("Unable to create directory '$response'.  " .
                      "Error=$ERRNO\n");
            } else {
                $linkDir = $response;
            }
        }
    }
    print("\n");

    return $linkDir;
}



sub installStaticLib($$$) {

    my ($pkgdir, $prefix, $linkdirR) = @_;

    if (-d("$pkgdir/staticlink")) {
        my $linkDir = getStaticLinkDir($prefix);

        print("Installing static link libraries.\n");

        my $rc = system("$cpCommand $pkgdir/staticlink/* $linkDir/");

        if ($rc != 0) {
            print("Copy of files from $pkgdir/staticlink " .
                  "to $linkDir failed.\n");
            print("cp return code is $rc\n");
        } else {
            print("done.\n");
        }
        $$linkdirR = $linkDir;
    } else {
        print("You did not build a static library, so I will not " .
              "install one \n");
        $$linkdirR = undef;
    }
}



sub getDataDir($) {
#-----------------------------------------------------------------------------
#  Find out from the user where he wants the runtime data files installed and
#  return that.
#-----------------------------------------------------------------------------
    my ($prefix) = @_;

    print("Where do you want the data files installed?\n");
    print("\n");

    my $dataDir;

    while (!$dataDir) {
        my $default = "$prefix/lib";

        my $response = fsObjPrompt("data file directory", $default);
        
        if (-d($response)) {
            $dataDir = $response;
        } else {
            my $succeeded = mkdir($response, 0777);
            
            if (!$succeeded) {
                print("Unable to create directory '$response'.  " .
                      "Error=$ERRNO\n");
            } else {
                $dataDir = $response;
            }
        }
    }
    print("\n");

    return $dataDir;
}



sub getHdrDir($) {
#-----------------------------------------------------------------------------
#  Find out from the user where he wants the compile-time header files
#  installed and return that.
#-----------------------------------------------------------------------------
    my ($prefix) = @_;

    print("Where do you want the library interface header files installed?\n");
    print("\n");

    my $hdrDir;

    while (!$hdrDir) {
        my $default = "$prefix/include";

        my $response = fsObjPrompt("header directory", $default);
        
        if (-d($response)) {
            $hdrDir = $response;
        } else {
            my $succeeded = mkdir($response, 0777);
            
            if (!$succeeded) {
                print("Unable to create directory '$response'.  " .
                      "Error=$ERRNO\n");
            } else {
                $hdrDir = $response;
            }
        }
    }
    print("\n");

    return $hdrDir;
}



sub installDataFile($$$) {

    my ($pkgdir, $prefix, $datadirR) = @_;

    my $dataDir = getDataDir($prefix);

    print("Installing data files...\n");

    my $rc = system("$cpCommand $pkgdir/misc/* $dataDir/");

    if ($rc != 0) {
        print("copy of data files from $pkgdir/misc to $dataDir " .
              "failed.\n");
        print("cp exit code is $rc\n");
    } else {
        $$datadirR = $dataDir;
        print("done.\n");
    }
}



sub installHeader($$$) {

    my ($pkgdir, $prefix, $includedirR) = @_;

    my $hdrDir = getHdrDir($prefix);

    print("Installing interface header files...\n");

    my $rc = system("$cpCommand $pkgdir/include/* $hdrDir/");

    if ($rc != 0) {
        print("copy of header files from $pkgdir/include to $hdrDir " .
              "failed.\n");
        print("cp exit code is $rc\n");
    } else {
        # Install symbolic links for backward compatibility (because the
        # netpbm/ subdirectory wasn't used before Netpbm 10.41 (December
        # 2007).

        my $rc = system("cd $hdrDir; ln -s netpbm/* .");

        if ($rc != 0) {
            print("Failed to create backward compatibilty symlinks from " .
                  "$hdrDir into $hdrDir/netpbm\n");
            print("ln exit code is $rc\n");
        } else {
            print("done.\n");
        }
    }
    $$includedirR = $hdrDir;
}



sub netpbmVersion($) {
    my ($pkgdir) = @_;

    my $versionOpened = open(VERSION, "<$pkgdir/VERSION");

    my $version;
    my $error;

    if (!$versionOpened) {
        $error = "Unable to open $pkgdir/VERSION for reading.  " .
            "Errno=$ERRNO\n";
    } else {
        $version = <VERSION>;
        chomp($version);
        close(VERSION);
    }

    if ($error) {
        print("Failed to determine the version of Netpbm from the package, "
              . "so that will not be correct in netpbm.config and netpbm.pc.  "
              . $error . "\n");
        $version = "???";
    }
    return $version;
}



sub 
processTemplate($$$) {
    my ($templateR, $infoR, $outputR) = @_;

    my @output;

    foreach (@{$templateR}) {
        if (m{^@}) {
            # Comment -- ignore it.
        } else {
            if (defined($infoR->{VERSION})) {
                s/\@VERSION\@/$infoR->{VERSION}/;
            }
            if (defined($infoR->{BINDIR})) {
                s/\@BINDIR@/$infoR->{BINDIR}/;
            }
            if (defined($infoR->{LIBDIR})) {
                s/\@LIBDIR@/$infoR-.{LIBDIR}/;
            }
            if (defined($infoR->{LINKDIR})) {
                s/\@LINKDIR@/$infoR->{LINKDIR}/;
            }
            if (defined($infoR->{DATADIR})) {
                s/\@DATADIR@/$infoR->{DATADIR}/;
            }
            if (defined($infoR->{INCLUDEDIR})) {
                s/\@INCLUDEDIR@/$infoR->{INCLUDEDIR}/;
            }
            push(@output, $_);
        }
    }
    $$outputR = \@output;
}



sub installConfig($$) {
    my ($installdir, $templateSubsR) = @_;
#-----------------------------------------------------------------------------
# Install 'netpbm-config' -- a program you run to tell you things about
# how Netpbm is installed.
#-----------------------------------------------------------------------------
    my $error;

    my $configTemplateFilename = dirname($0) . "/config_template";

    my $templateOpened = open(TEMPLATE, "<$configTemplateFilename");
    if (!$templateOpened) {
        $error = "Can't open template file '$configTemplateFilename'.\n";
    } else {
        my @template = <TEMPLATE>;

        close(TEMPLATE);

        processTemplate(\@template, $templateSubsR, \my $fileContentsR);

        # TODO: Really, this ought to go in an independent directory,
        # because you might want to have the Netpbm executables in
        # some place not in the PATH and use this program, via the
        # PATH, to find them.
        
        my $filename = "$installdir/netpbm-config";
        
        my $success = open(NETPBM_CONFIG, ">$filename");
        if ($success) {
            chmod(0755, $filename);
            foreach (@{$fileContentsR}) { print NETPBM_CONFIG; }
            close(NETPBM_CONFIG);
        } else {
            $error = "Unable to open the file " .
                "'$filename' for writing.  Errno=$ERRNO\n";
        }
    }
    if ($error) {
        print(STDERR "Failed to create the Netpbm configuration program.  " .
              "$error\n");
    }
}




sub getPkgconfigDir($) {
#-----------------------------------------------------------------------------
#  Find out from the user where he wants the Pkg-config file for the
#  installation (netpbm.pc) and return that.
#-----------------------------------------------------------------------------
    my ($prefix) = @_;

    print("Where do you want the Pkg-config file netpbm.pc installed?\n");
    print("\n");

    my $pkgconfigDir;

    while (!$pkgconfigDir) {
        my $default = "$prefix/lib/pkgconfig";

        my $response = fsObjPrompt("Pkg-config directory", $default);
        
        if (-d($response)) {
            $pkgconfigDir = $response;
        } else {
            my $succeeded = mkdir($response, 0777);
            
            if (!$succeeded) {
                print("Unable to create directory '$response'.  " .
                      "Error=$ERRNO\n");
            } else {
                $pkgconfigDir = $response;
            }
        }
    }
    print("\n");

    return $pkgconfigDir;
}



sub installPkgConfig($$) {
    my ($prefix, $templateSubsR) = @_;
#-----------------------------------------------------------------------------
# Install a pkg-config file (netpbm.pc) - used by the 'pkg-config' program to
# find out various things about how Netpbm is installed.
#-----------------------------------------------------------------------------
    my $pkgconfigDir = getPkgconfigDir($prefix);

    my $error;

    my $pcTemplateFilename = dirname($0) . "/pkgconfig_template";

    my $templateOpened = open(TEMPLATE, "<$pcTemplateFilename");
    if (!$templateOpened) {
        $error = "Can't open template file '$pcTemplateFilename'.\n";
    } else {
        my @template = <TEMPLATE>;

        close(TEMPLATE);

        processTemplate(\@template, $templateSubsR,
                        \my $fileContentsR);

        my $filename = "$pkgconfigDir/netpbm.pc";
        
        my $success = open(NETPBM_PC, ">$filename");
        if ($success) {
            chmod(0755, $filename);
            foreach (@{$fileContentsR}) { print NETPBM_PC; }
            close(NETPBM_PC);
        } else {
            $error = "Unable to open the file " .
                "'$filename' for writing.  Errno=$ERRNO\n";
        }
    }
    if ($error) {
        print(STDERR "Failed to create the Netpbm Pkg-config file.  " .
              "$error\n");
    }
}



#******************************************************************************
#
#  MAINLINE
#
#*****************************************************************************

autoFlushStdout();

print("Welcome to the Netpbm install dialogue.  We will now proceed \n");
print("to interactively install Netpbm on this system.\n");
print("\n");
print("You must have already built Netpbm and then packaged it for \n");
print("installation by running 'make package'.  See the INSTALL file.\n");
print("\n");

my $pkgdir = getPkgdir();

print("Installing from package directory '$pkgdir'\n");
print("\n");

my $prefix = getPrefix();

print("Using prefix '$prefix'\n");
print("\n");

$cpCommand = getCpCommand();

installProgram($pkgdir, $prefix, \my $bindir);
print("\n");

installSharedLib($pkgdir, $prefix, \my $libdir);
print("\n");

installSharedStub($pkgdir, $prefix, $libdir, \my $sharedlinkdir);
print("\n");

installStaticLib($pkgdir, $prefix, \my $staticlinkdir);
print("\n");

installDataFile($pkgdir, $prefix, \my $datadir);
print("\n");

installHeader($pkgdir, $prefix, \my $includedir);
print("\n");

my $linkdir = defined($sharedlinkdir) ? $sharedlinkdir : $staticlinkdir;

my $templateSubsR =
    {VERSION    => netpbmVersion($pkgdir),
     BINDIR     => $bindir,
     LIBDIR     => $libdir,
     LINKDIR    => $linkdir,
     DATADIR    => $datadir,
     INCLUDEDIR => $includedir,
    };

installConfig($bindir, $templateSubsR);

installPkgConfig($prefix, $templateSubsR);

print("Installation is complete (except where previous error messages have\n");
print("indicated otherwise).\n");

exit(0);
