=begin
  Insert or Update image data to pubs2..au_pix on au_pix = "my-image-1"

  ex.
  ruby -I ../  sendimage.rb -S SYBASE -U sa -P XXXXXX somefile.jpg

  You must install pubs2 database beforehand.
    see,
      $SYBASE/scripts/installpubs2
	  and
      $SYBASE/scripts/installpix2

  
=end

require "sybsql.rb"
# require "getopts.rb"

class SybSQL
  if (SybSQL.freetds?) then
    def set_textsize (size)
      self.sql_norow("set textsize #{size}") 
    end
  else
    def set_textsize(size)
      self.connection.setopt(CS_OPT_TEXTSIZE, size)
    end
  end
end

# 'select into/bulkcopy' option set
def set_bulkcopy (query, dbname, status )
  begin
    unless( query.sql_norow("use master") ) then
      return false
    end
    query.sql("exec sp_dboption #{dbname},'select into/bulkcopy',#{status}")
    return false unless (query.cmd_done? )
    return false if( query.top_status_result != 0 )

    unless( query.sql_norow("use #{dbname}") ) then
      return false 
    end
    unless( query.sql_norow("checkpoint") ) then
      return false
    end
  rescue
    return false
  else
    return true
  end
end

$ImageFile = nil

$lang = ENV['LANG']
$lang = 'default' unless $lang

$server = "SYBASE"
$user = "sa"
$passwd = ""

while ( !ARGV.empty? )
  case arg=ARGV.shift
  when /^-S$/, /^-S(.*)$/
    $server = $1 || ARGV.shift
  when /^-U$/, /^-U(.*)$/
    $user = $1 || ARGV.shift
  when /^-P$/, /^-P(.*)$/
    $passwd = $1 || ARGV.shift
  when /^-J$/, /^-J(.*)$/
    $lang = $1 || ARGV.shift
  when /[^-].+/
    $ImageFile = arg
  else
    $stderr.print "usage: ",$0,
      "  image-file [-S server -U user -P passwd -J lang ]\n"
    exit 1
  end
end

#print [$server,$user,$passwd,$lang,$ImageFile],"\n" # debug

if( !$ImageFile || !(FileTest.file?($ImageFile) ) ) then
  print "NoFile\n"
  exit
end

is_delete = false

begin
  cmd = nil
  query=SybSQL.new({'S'=>$server,'U'=>$user, 'P'=>$passwd,
		     'appname'=>"sendimage", 'lang'=>$lang} )

  # Set TEXT/IMAGE API limitation
  unless( query.connection.setprop(CS_TEXTLIMIT, CS_NO_LIMIT)) then
    $stderr.print("NG setprop(CS_TEXTLIMIT)\n");
  end
  # MAX 1M bytes
  unless( query.set_textsize(1024 * 1024)) then
    $stderr.print("NG setopt(CS_OPT_TEXTSIZE)\n");
  end

  # 'select into/bulkcopy' option set and USE PUBS2
  unless ( set_bulkcopy( query, 'pubs2', 'true' ) ) then
    raise "failed set_bulkcopy"
  end

  # check row exists
  query.sql('select count(*) from au_pix where au_id = "my-image-1"')
  raise "Failed select count"  unless (query.cmd_done? )
  res = query.top_row_result

  if( res.nthrow(0,0) <= 0 ) then
    # Insert row if not exist "my-image-1" pic
    sql = 'insert au_pix (au_id,format_type) values ("my-image-1", "JPG")'
    unless( query.sql_norow(sql) ) then
      raise "failed, insert"
    end
    rcnt = query.nth_results(0, SybConstant::CS_CMD_SUCCEED).row_count
    print "insert OK  (#{rcnt} rows affected)\n"

    # Updates NULL. ( ??? only INSERT cannot retrieve IODESC  ???)
    sql = 'update au_pix set pic=NULL where au_id = "my-image-1"'
    unless( query.sql_norow(sql) ) then
      raise "failed, update"
    end
    rcnt = query.nth_results(0, SybConstant::CS_CMD_SUCCEED).row_count
    print "NULL update OK  (#{rcnt} rows affected)\n"
  end
  is_delete = true

  if( $ImageFile ) then
    query.image_transize( 2048 )
    query.image_log( false )  # Not use LOG
    begin
      # Get IODESC
      sql = 'select au_id,pic from au_pix where au_id = "my-image-1"'
      query.sql_iodesc(sql,2)
      iodesc = query.top_row_result.nthrow(0,1)
      raise "Cannot fetch IODESC" unless( iodesc.kind_of?(SybIODesc) )
      if( $DEBUG )then
	print "IODESC before update\n"
	print iodesc.inspect,"\n"
      end
      query.send_imagefile(iodesc, $ImageFile)
      if( $DEBUG )then
	print "IODESC after update\n"
	print iodesc.inspect,"\n"
      end
      print "Success Update Image\n"
    rescue
      print "Fail Update image \n"
      print "\t#{$!}\n"
      if( $@.kind_of?(Array) )then
	$@.each{ |s| print "\t",s,"\n" }
      end
    end
  end

rescue
  print "ERROR: ", $!,"\n"
  if( $@.kind_of?(Array) )then
    $@.each{ |s| print "  ",s,"\n" }
  end
ensure
  if(query.kind_of?( SybSQL ) ) then
    # is_delete = false
    if( is_delete ) then
      print "Delete ?(no) " ; $stdout.flush
      ans = $stdin.gets
      if( ans =~ /^\s*y/io ) then
	unless( query.sql_norow('delete au_pix where au_id = "my-image-1"') ) then
	  raise "failed, delete"
	end
	rcnt = query.nth_results(0, SybConstant::CS_CMD_SUCCEED).row_count
	print "delete OK (#{rcnt} rows affected)\n"
      end
    end

    # 'select into/bulkcopy' option clear
    set_bulkcopy(query, 'pubs2', 'false' )

    print "query close\n" if( $DEBUG)
    query.close
    query = nil
  end
end

print "END\n"

  
