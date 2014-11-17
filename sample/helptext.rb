#! /usr/local/bin/ruby
=begin
usage:
    helptext.rb procedure-name options
options:    
  -f filename 
      The default is procedure-name.sql
      -    stdout
  -script
      Append "drop proc" etc.
  -noexists
    If output-file exists already, not run
  -d output dirname
  -S server-name (default is SYBASE)
  -U user-name   (default is sa)
  -P password ("")
  -t timeout (default is 0)
  -D dbname (default is sybsystemprocs)
  -J locale name 
    The default is $LANG

e.g.
  % ruby helptext.rb  -S SYBASE -U sa -P XXXXXX -D pubs2 title_proc 
  % cat title_proc.sql
=end

require "sybsql.rb"

retstat = 1

lang = ENV['LANG']
lang = 'default' unless lang

server = "SYBASE"
user = "sa"
passwd = ""
timeout = 0
dbname = "sybsystemprocs"
procname = "sp_helptext"
filename = nil
dirname = nil
script = false
existcheck = false

while ( !ARGV.empty? )		
    case (arg = ARGV.shift )
    when /^-f$/, /^-f(.*)$/
      filename = $1 || ARGV.shift
    when /^-d$/, /^-d(.*)$/
      dirname = $1 || ARGV.shift
    when /^-S$/, /^-S(.*)$/
      server = $1 || ARGV.shift
    when /^-U$/, /^-U(.*)$/
      user = $1 || ARGV.shift
    when /^-P$/, /^-P(.*)$/
      passwd = $1 || ARGV.shift
    when /^-J$/, /^-J(.*)$/
      lang = $1 || ARGV.shift
    when /^-t$/, /^-t(.*)$/
      timeout = $1 || ARGV.shift
      timeout = timeout.to_i
    when /^-D$/, /^-D(.*)$/
      dbname = $1 || ARGV.shift
    when /^-script$/
      script = true
    when /^-noexists$/
      existcheck = true
    when /^-h$/
      $stderr.print "usage: ",$0, "procedure-name", 
	"  [-f filename -S server -U user -P passwd -J lang -t timeout-sec]\n"
      exit 1
    else
      procname = arg
    end
end

file = nil
textbuf = ""

begin

  query=SybSQL.new(
		 {'S'=>server,'U'=>user,'P'=>passwd, 'timeout' => timeout,
		     'appname'=>'helptext.rb','lang'=>lang}
		 )
  
  unless( query.sql_norow("use #{dbname}") ) then
    raise "failed, use dbname"
  end

  query.set_strip(false)

  sql = "select text from syscomments " +
    "where id = object_id('#{procname}') order by colid2, colid"
  $stderr.print "SQL=",sql,"\n" if( $DEBUG )

  query.sql(sql)
  # CMD_DONE exists?
  raise "failed, SELECT" unless (query.cmd_done? )

  # first ROWRESULT 
  res = query.top_row_result
  raise "NO procedure #{procname}" if(res.row_count <= 0 )

  padfill_count = 0  # number of white-space padding
  res.rows.each {
    |r| 
    if( r[0].kind_of?(String) ) then
      text = r[0]
      # Handle padding request of last process
      if( padfill_count > 0 ) then
	# (if (and "The first character is KANJI in this time"
        #          "One space padding in last time" )
	#     (no-padding) )
	if( (padfill_count != 1) || (text[0] < 128) ) then
	  textbuf.concat( " " * padfill_count )
	end
      end

      $stderr.print "length=", text.size, "\n" if( $DEBUG )
      textbuf.concat( text )
      # Length does pad of a part doing not reach 255 with the next time, blank.
      padfill_count = 255 - text.size
    else
      $stderr.print "ERROR: Not String\n"
    end
  }
  # copy to file
  if( filename == "-" ) then
      $stdout.write(textbuf);
  else 
    # define filename
    if( filename == "RCS") then
      # search RCS header file
      if( textbuf =~ /\/\*\s*\$Header\:\s.*?([^\/\,]+)\,v/io ) then
	$stderr.print $1,"\n" if( $DEBUG )
	filename = $1
      else
	$stderr.print("No RCS header\n") if( $DEBUG )
	filename = nil
      end
    end
    if (filename.nil? || (filename.strip == "" ) )then
      filename = "#{procname}.sql" 
    end
    if( ! dirname.nil? )then
      dirname.chomp!("/")
      filename = "#{dirname}/#{filename}"
    end
    $stderr.print "Output to #{filename}\n" if( $DEBUG )
    
    # Not run, if file exists.
    if( existcheck &&  test(?e, filename ) ) then
      $stderr.print("#{procname}:  #{filename} already exists!\n")
    else
      file = File.open(filename,"w")
      if( script )then
	file.print "drop proc #{procname}\ngo\n"
      end
      file.write(textbuf)
      if( script )then
	file.print "go\n"
      end
    end
  end
  retstat = 0
rescue
  print "ERROR: ", $!,"\n"
  if( $@.kind_of?(Array) )then
    $@.each{ |s| print "  ",s,"\n" }
  end
ensure
  query.close if(query.kind_of?( SybSQL ) );
  query = nil
  if( file.kind_of?(File) ) then
    $stderr.print "File close" if( $DEBUG )
    file.close
  end
  exit retstat
end

