require 'mkmf'

####### For cygwin + windows XP ##########################
#
sybase = "/cygdrive/c/sybase"
sybase_ocs = "#{sybase}/OCS-12_5"
$CFLAGS = "-g -Wall -D_MSC_VER=800 -I#{sybase_ocs}/include" 
$LDFLAGS = "-L./cygwin-lib"
$LOCAL_LIBS = "-lct -lcs -lm"

## DO NOT EDIT BELOW THIS LINE ####################
def ruby_version_high?( ver )
  vruby = RUBY_VERSION.split('.')
  ver = ver.split('.')
  vruby.each{
    |v|
    v = v.to_i
    nver = ver.shift
    return true if( nver.nil?)
    nver = nver.to_i
    return true if( v > nver )
    return false if( v < nver )
  }
  return true if( ver.empty?)
  return false
end

# Check ruby version , check STR2CSTR obsolete?
use_str2cstr = "-DUSE_STR2CSTR"
use_str2cstr = "" if (ruby_version_high?("1.7.0") )

$CFLAGS = "#{$CFLAGS} #{use_str2cstr}"
create_makefile("sybct")





