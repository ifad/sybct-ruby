=begin
  Get image data from pubs2..au_pix table

  ex.
  mkdir tmp
  ruby -I ../ getimage.rb -U sa -S SYBASE -P XXXXXX ./tmp
  cd tmp 
  xv & 

  You must install pubs2 database beforehand.
    see,
      $SYBASE/scripts/installpubs2
	  and
      $SYBASE/scripts/installpix2

=end

require "sybsql.rb"

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

lang = ENV['LANG']
lang = 'default' unless lang

server = "SYBASE"
user = "sa"
passwd = ""
timeout = 0

while ( !ARGV.empty? )
  case arg=ARGV.shift
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
  when /^[^-]/
    $ImageDir = arg
  else
    $stderr.print "usage: ",$0,
      "  [-S server -U user -P passwd -J lang -t timeout-sec]\n"
    exit 1
  end
end

$ImageDir = "." if ( $ImageDir.nil? )
print $ImageDir,"\n"

begin
  query=SybSQL.new({'S'=>server,'U'=>user,'P'=>passwd, 'timeout' => timeout,
		     'appname'=>'getimage','lang'=>lang,
		     'async'=>true, 
		   })

  # Set TEXT/IMAGE API limitation
  unless( query.connection.setprop(CS_TEXTLIMIT, CS_NO_LIMIT)) then
    $stderr.print("NG setprop(CS_TEXTLIMIT)\n");
  end
  # MAX 1M bytes
  unless( query.set_textsize(1024 * 1024) ) then
      $stderr.print("NG setopt(CS_OPT_TEXTSIZE)\n");
  end

  # use pubs2
  unless( query.sql_norow("use pubs2") ) then
    raise "failed, use pubs2"
  end

  query.set_strip(true)
  query.image_transize(4096)

  sql = "select au_id,format_type,pic from au_pix "
  image_column = 3
  file = nil
  rsize = 0
  fname = ""
  query.sql_getimage(sql, image_column){
    |rid,r,cid,clm, data|
    if( data.kind_of?(String) )then
      if( file.nil? )then
	ftype = r[1].downcase
	ftype = "ras" if( ftype =~ /ras.*/io )
	fname = "#{r[0]}.#{ftype}"
	file = File.open("#{$ImageDir}/#{fname}","w")
	rsize = 0
      end
      file.write(data)
      rsize = rsize + data.length
      print "#{fname}: #{rsize} bytes read\n"
    else 
      file.close if( file.kind_of?(File))
      file = nil
      raise "Error in sql_getimage\n" if( data == false )
    end
  }
  print "END\n"
rescue
  print "ERROR: ", $!,"\n"
  if( $@.kind_of?(Array) )then
    $@.each{ |s| print "  ",s,"\n" }
  end
ensure
  if(query.kind_of?( SybSQL ) ) then
    query.close
    query = nil
  end
end

  
