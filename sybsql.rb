#  Ruby module for SyBase.
#  Copyright (C) 1999-2001  Tetsuhumi Takaishi

require "sybct.rb"

# ex.
# query=SybSQL.new({'S'=>'SYBASE','U'=>'sa','P'=>'',\
# 'appname'=>'ruby','lang'=>'ja_JP.ujis'} )
# raise("ERR") unless (query.sql_norow("use master"))
# res = query.sql("select * from sysservers")
# print res[0].columns.join("|")
# res[0].rows.each{ |r| print "#{r.join('|')}\n" }
# res = query.sql("set rowcount 2\nselect * from XXX\n")
# res = query.sql("set rowcount 3\nselect * from XXX select * from YYY\n")
# sql.close
# sql = nil
# GC.start

####  Stores Sybase result set ####
# @restype	command result type ( See SybCommand)
# @rows		Array of row result
#		  Only when @restype is fetchable( ROW_RESULT,ROW_RESULT )
# @columns	List of column name
#		  Only when @restype is fetchable( ROW_RESULT,ROW_RESULT )
# @colhash	HASH -- { "column-name" => "column-order" }
#		 Only when @restype is fetchable ( ROW_RESULT,ROW_RESULT )
# @tran_state	The transactions state
#		  Only when @restype is  CMD_SUCCEED , CMD_DONE or CMD_FAIL
# @row_count	Row count affected by the command
#		  Only when @restype is  CMD_SUCCEED , CMD_DONE or CMD_FAIL
class SybResult
  attr( "colhash" )

  ASCII_RESTYPE = {
    SybConstant::CS_CMD_SUCCEED => 'CMD_SUCCEED',
    SybConstant::CS_CMD_DONE => 'CMD_DONE',
    SybConstant::CS_CMD_FAIL => 'CMD_FAIL',
    SybConstant::CS_ROW_RESULT => 'ROW_RESULT',
    SybConstant::CS_CURSOR_RESULT => 'CURSOR_RESULT',
    SybConstant::CS_PARAM_RESULT => 'PARAM_RESULT',
    SybConstant::CS_STATUS_RESULT => 'STATUS_RESULT',

    SybConstant::CS_COMPUTE_RESULT => 'COMPUTE_RESULT',
    SybConstant::CS_COMPUTEFMT_RESULT => 'COMPUTEFMT_RESULT',
    SybConstant::CS_ROWFMT_RESULT => 'ROWFMT_RESULT',
    SybConstant::CS_MSG_RESULT => 'MSG_RESULT',
    SybConstant::CS_DESCRIBE_RESULT => 'DESCRIBE_RESULT',
  }

  # rowset <= [ columns-array, rows-array ]
  # ex.  r = SybResult.new(3,[ ["a","b","c"], [[1,2,3],[10,20,30]] ])
  def initialize ( type, rowset=nil, rcnt=nil, tstate=nil)
    @restype = type
    @columns = nil
    @rows = nil
    @tran_state = tstate
    @row_count = rcnt
    if ( rowset.is_a?( Array ) ) then
      @columns = rowset[0]
      @rows = rowset[1]
      if( @columns.is_a?( Array ) )then
	@colhash = {}
	@columns.each_index { |id| @colhash[ @columns[id] ] = id }
      end
    end
  end

  def restype
    return @restype
  end

  def rows
    return @rows
  end

  def columns
    return @columns
  end

  def nthrow( nth, col=nil )
    return nil unless( @rows )
    row = @rows[ nth ]
    return nil unless (row)

    if( col )then
      if( col.is_a?( String ) )then
	col = @colhash[ col ]
      end
      return nil unless( col )
      return row[ col ]
    end
    return row
  end

  def to_s
    if( @restype.is_a?( Numeric ) ) then
      type = ASCII_RESTYPE[ @restype ]
      type = @restype.to_s  unless ( type )
    else
      type = @restype.to_s
    end
    return  '(' + "type=#{type} " + ' columns=' + @columns.to_s + 
    ' rows=' + @rows.to_s + ')'
  end

  def tran_state
    @tran_state
  end

  def row_count
    return @rows.length if( @rows.kind_of?( Array ) )
    return @row_count
  end
end

class SybResultArray < Array
  def each_results( type = SybConstant::CS_ROW_RESULT)
    self.each { |r|
      if( r.restype == type ) then
	yield(r)
      end
    }
  end

  def nth_results(nth=0, type = SybConstant::CS_ROW_RESULT )
    cnt = 0
    self.each{ |r|
      if( r.kind_of?( SybResult ) ) then
	if( r.restype == type ) then
	  return r if( nth == cnt)
	  cnt += 1   # Not ++cnt
	end
      end
    }
    return nil
  end

  # As a result of command processing, Did failure fit even one?
  # (Is CS_CMD_FAIL even one?)
  def cmd_fail?
    return true if( nth_results(0, SybConstant::CS_CMD_FAIL) != nil )
    return false
  end

  # Have been the results of a command  completely processed ?
  # ( CS_CMD_FAIL.counts == 0 AND  CS_CMD_DONE.counts >= 1) ?
  def cmd_done?
    return false if ( cmd_fail? )
    return true if( nth_results(0, SybConstant::CS_CMD_DONE) )
    return false
  end

  # Returns TRUE, if the command returning no rows completely successfully.
  # (CS_CMD_FAIL.counts == 0 AND CS_CMD_SUCCEED.counts >= 1) ?
  def cmd_succeed?
    return false if ( cmd_fail? )
    return true if( nth_results(0, SybConstant::CS_CMD_SUCCEED) )
    return false
  end

  def top_row_result
    nth_results(0, SybConstant::CS_ROW_RESULT)
  end
  def top_param_result
    nth_results(0, SybConstant::CS_PARAM_RESULT)
  end
  def top_status_result
    res = nth_results(0, SybConstant::CS_STATUS_RESULT)
    if( res.kind_of?( SybResult ) ) then
      return res.nthrow(0,0)
    end
  end
end

class SybSQLContext < SybContext

  def timeout?
    return @timeout_p if( defined?(@timeout_p) )
    return false
  end

  def reset_timeout
    @timeout_p = false
  end

  # redefine defualt callback
  def srvmsgCB( con,msghash)
    return true unless ( msghash.kind_of?(Hash) )
    if (( msghash[ "severity" ] == 0) || ( msghash[ "severity" ] == 10)) then
      return true
    end
    
    $stderr.print "\n** SybSQLContext Server Message: **\n"
    $stderr.print "  Message number #{msghash['msgnumber']} ",
    "Severity #{msghash['severity']} ",
    "State #{msghash['state']} ", "Line #{msghash['line']} \n"
    
    $stderr.print "  Server #{msghash['srvname']}\n"
    $stderr.print "  Procedure #{msghash['proc']}\n"
    $stderr.print "  Message String:  #{msghash['text']}\n"

    return true
  end

  def cltmsgCB( con,msghash)
    # For debug 
    # print "gc start\n"
    # GC.start
    # print "gc end\n"
    # Does not process about simply informational message.
    return true unless ( msghash.kind_of?(Hash) )
    unless ( msghash[ "severity" ] ) then
      return true
    end
    
    $stderr.print "\n** SybSQLContext Client-Message: **\n"

    $stderr.print "  Message number: ",
      "LAYER=#{msghash[ 'layer' ]} ", "ORIGIN=#{msghash[ 'origin' ]} ",
    "SEVERITY=#{msghash[ 'severity' ]} ", "NUMBER=#{msghash[ 'number' ]}\n"
    
    $stderr.print "  Message String: #{msghash['msgstring']}\n"
    $stderr.print "  OS Error: #{msghash['osstring']}\n"

    # Not retry , CS_CV_RETRY_FAIL( probability TimeOut ) 
    if( msghash[ 'severity' ] == "RETRY_FAIL" ) then
      @timeout_p = true
      return false
    end
    return false  # Change (ver 0.2.6)
  end
end

class SybTimeOutError<StandardError
end

class SybSQL

  #attr "bind_numeric2fixnum" , true
  attr_accessor "bind_numeric2fixnum"

  # initialize( { 'S'=>, 'U'=>, &REST,
  #              'lang'=>, 'timeout'=>, 'P'=>, 'appname'=>, 'hostname'=>,
  #              'async'=> } )
  def initialize (hash, context_class=SybSQLContext)
    raise("Not server") unless ( hash[ 'S' ] )
    raise("Not user") unless ( hash[ 'U' ] )

    raise("Not SybContext class") if ( context_class.kind_of?(SybContext) )
    arg = [ hash['lang'], hash['timeout'], hash['async'] ]
    # @ctx = SybSQLContext.create( *arg );
    @ctx = context_class.create( *arg );
    if( hash['async'] == true ) then
      @ctx.setprop(CS_TIMEOUT, CS_NO_LIMIT)
    end
    @ctx.reset_timeout

    if( hash[ 'login-timeout'].to_i > 0 )then  # Add (Ver 0.2.6)
      @ctx.setprop(CS_LOGIN_TIMEOUT, hash['login-timeout'].to_i)
      print "login timeout is #{@ctx.getprop(CS_LOGIN_TIMEOUT)}\n" if( $DEBUG)
    end

    @conn = nil;
    arg = [ 
      @ctx,
      hash['S'], hash['U'], hash['P'],
      hash['appname'], hash['hostname'], hash['serveraddr']
    ]

    @conn = SybConnection.open( *arg );
    @results = nil
    @strip = false

    # Image/Text
    @image_transize = 1024
    @image_log = false

    # SybCommand set
    @fetch_rowfail = false

    # SybSql#bind_numeric2fixnum ( Thanks to Will Sobel )
    #  If set true, SybSql#query fetch numeric(N,0) as ruby's fixnum. #'
    #  However, N is restricted to <= 10
    #  ex.
    #   query=SybSQL.new(....)
    #     .................
    #   query.bind_numeric2fixnum = true
    #    .................
    #   query.sql("select ...")  
    @bind_numeric2fixnum = false

  end

  def context
    return @ctx
  end
  def connection
    return @conn
  end

  def close(force=false)
    # @conn.close(true) if( @conn.kind_of?( SybConnection ) );
    if( @conn.kind_of?( SybConnection ) ) then
      @conn.close(force)
      @conn.delete(true)
    end
    @conn = nil;
    @ctx.destroy(true) if( @ctx.kind_of?( SybContext ) )
    @ctx = nil
  end

  # set rowcount N (N=0 is clear )
  if SybConstant::IS_FREETDS then
    def set_rowcount(maxrows)
      return false unless( @conn.kind_of?( SybConnection ) )
      return sql_norow("set rowcount #{maxrows}") 
    end
  else
    def set_rowcount(maxrows)
      return false unless( @conn.kind_of?( SybConnection ) )
      return @conn.setopt(CS_OPT_ROWCOUNT, maxrows)
    end
  end

  def set_forceplan( on )  # on: true/false
    return false unless( @conn.kind_of?( SybConnection ) )
    return @conn.setopt(CS_OPT_FORCEPLAN, on)
  end

  def sql( commands, nbind=nil, &rowproc )
    begin
      @results = nil
      @ctx.reset_timeout
      cmd = SybCommand.new(@conn, commands)
      cmd.fetch_rowfail( @fetch_rowfail )
      rowproc = nil unless( rowproc.kind_of?(Proc) ) # No use!
      re = query(cmd, nbind, rowproc)
      @results = re if( re.kind_of?(SybResultArray) )
      return re
    ensure
      if( timeout? )then
	print "--SybSQL.sql timeout\n" if( $DEBUG )
	@conn.close(true)
	cmd.delete if( cmd.kind_of?(SybCommand));
	@conn.delete(true)
	@conn = nil
	raise SybTimeOutError, "#{commands} timeout"
      else
	print "SybSQL.sql Command delete\n" if( $DEBUG )
	cmd.delete if( cmd.kind_of?(SybCommand));
	print "SybSQL.sql Command delete END\n" if( $DEBUG )
      end
    end
  end

  def do_cursor( cname, command, rowcount=nil, curopt=nil, nbind=nil, &rowproc )
    begin
      @results = nil
      @ctx.reset_timeout

      cmd = SybCommand.cursor_new(@conn, cname, command, curopt)
      cmd.fetch_rowfail( @fetch_rowfail )
      cmd.cursor_rows(rowcount) if( ! rowcount.nil? )
      cmd.cursor_open

      rowproc = nil unless( rowproc.kind_of?(Proc) ) # No use!
      re = query(cmd, nbind, rowproc)
      @results = re if( re.kind_of?(SybResultArray) )
      return re
    ensure
      if( timeout? )then
	print "--SybSQL.sql timeout\n" if( $DEBUG )
	@conn.close(true)
	cmd.delete if( cmd.kind_of?(SybCommand));
	@conn.delete(true)
	@conn = nil
	raise SybTimeOutError, "#{command} timeout"
      else
	$stderr.print "SybSQL.do_cursor Command delete\n" if( $DEBUG )
	if( cmd.kind_of?(SybCommand) ) then
	  $stderr.print "cstat=",cmd.cursor_state,"\n" if($DEBUG)
	  cmd.cursor_close	# close cursor
	  query(cmd)
	  $stderr.print "cstat=",cmd.cursor_state,"\n" if($DEBUG)
	end
	cmd.delete if( cmd.kind_of?(SybCommand));  # dealloc cursor
	$stderr.print "SybSQL.do_cursor Command delete END\n" if( $DEBUG )
      end
    end
  end

  # Execute RPC
  # CAUTION: No usable !!!!!
  # args
  #  RPC parameter array (See sybct.c::f_cmd_param source comments )
  # return:  
  #   SUCCESS	Array of SybResult 
  #   ERROR	FALSE
  def rpc( procname, *args )
    begin
      @results = nil
      @ctx.reset_timeout
      cmd = SybCommand.new(@conn, procname)
      cmd.fetch_rowfail( @fetch_rowfail )

      args.each {
	|arg|
	if( arg.kind_of?(Array )) then
	  raise("SybCommand.param( #{arg[0]} ) failed") unless cmd.param( *arg )
	else
	  raise("SybCommand.param( #{arg} ) failed") unless cmd.param( arg )
	end
      }

      re = query(cmd)
      @results = re if( re.kind_of?(SybResultArray) )
      return re
    ensure
      # print "Command delete\n"	# debug
      cmd.delete if( cmd.kind_of?(SybCommand));
    end
  end

  def set_strip(on)
    @strip = on
  end

  # nbind	The maximum binding column number when ROW_RESULT
  #             (nil -- all binding)
  # rowproc     Whenever one row is retrieved, this proc is called (default nil )
  # return
  #   SybResult		Array of Array for SybResult
  #   false		ERROR
  def query( cmd, nbind=nil, rowproc=nil )
    if( cmd.send != true )then
      return false
    end
      
    cmd.bind_numeric2fixnum = self.bind_numeric2fixnum

    result_list = SybResultArray.new
    while( true )
      type = cmd.results()
      case type
      when nil
	break
      when false
	return false
      when SybConstant::CS_CMD_SUCCEED
	# Success of a command that returns no row(such as INSERT,UPDATE)
	result_list.push( 
		     SybResult.new( type, nil, cmd.res_info(CS_ROW_COUNT),
				   cmd.res_info(CS_TRANS_STATE) ) )
      when SybConstant::CS_CMD_DONE
	# The results have been completely processed.
	result_list.push( 
		     SybResult.new( type, nil, cmd.res_info(CS_ROW_COUNT),
				   cmd.res_info(CS_TRANS_STATE) ) )
      when SybConstant::CS_CMD_FAIL
	# Error, while executing a command
	result_list.push( 
		     SybResult.new( type, nil, cmd.res_info(CS_ROW_COUNT),
				   cmd.res_info(CS_TRANS_STATE) ) )
      when SybConstant::CS_ROW_RESULT
	result_list.push( SybResult.new( type ,cmd.fetchloop(0,@strip,
							 nbind,rowproc)))
      when SybConstant::CS_CURSOR_RESULT
	result_list.push( SybResult.new( type ,cmd.fetchloop(0,@strip,
							 nbind,rowproc)))
      when SybConstant::CS_PARAM_RESULT
	result_list.push(  SybResult.new( type , cmd.fetchloop(0,@strip) ) )
      when SybConstant::CS_STATUS_RESULT
	result_list.push(  SybResult.new( type , cmd.fetchloop ) )
      when SybConstant::CS_COMPUTE_RESULT 	  # Unconfirmed , yet
	result_list.push(  SybResult.new( type , cmd.fetchloop(0,@strip) ) )
      else
	result_list.push(  SybResult.new(false) )
      end
      if ( timeout? ) then
	$stderr.print "Timeout in query\n" if( $DEBUG)
	return false
      end
    end
    return result_list
  end

  # return SybResultArray or nil
  def results
    return @results
  end

  def each_results( type = SybConstant::CS_ROW_RESULT)
    return nil unless ( @results.kind_of?( SybResultArray ) )
    @results.each_results(type) { |r| yield(r) }
  end

  def nth_results(nth=0, type = SybConstant::CS_ROW_RESULT )
    return nil unless ( @results.kind_of?( SybResultArray ) )
    @results.nth_results(nth, type)
  end

  # As a result of command processing, Did failure fit even one?
  # (Is CS_CMD_FAIL even one?)
  def cmd_fail?
    return false if( @results.nil? )
    @results.cmd_fail?
  end

  # Have been the results of a command  completely processed ?
  # ( CS_CMD_FAIL.counts == 0 AND  CS_CMD_DONE.counts >= 1) ?
  def cmd_done?
    return false if( @results.nil? )
    @results.cmd_done?
  end

  # Returns TRUE, if the command returning no rows completely successfully.
  # (CS_CMD_FAIL.counts == 0 AND CS_CMD_SUCCEED.counts >= 1) ?
  def cmd_succeed?
    return false if( @results.nil? )
    @results.cmd_succeed?
  end

  # return SybResult or nil
  def top_row_result
    return nil unless ( @results.kind_of?( SybResultArray ) )
    @results.top_row_result
  end

  # return SybResult or nil
  def top_param_result
    return nil unless ( @results.kind_of?( SybResultArray ) )
    @results.top_param_result
  end

  # return SybResult or nil
  def top_status_result
    return nil unless ( @results.kind_of?( SybResultArray ) )
    @results.top_status_result
  end

  def timeout?
    return self.context.timeout?
  end

  def reset_timeout
    self.context.reset_timeout
  end
  
  # return true/false
  def sql_norow (sql)
    return false unless( self.sql(sql) )
    return self.cmd_succeed?
  end

  # See sybct.rb
  def fetch_rowfail( val )
    @fetch_rowfail = val
  end

  # return connection status
  # return value is  SybConstant::CS_CONSTAT_DEAD, SybConstant::CS_CONSTAT_CONNECTED
  # bit OR
  def connection_status
    @conn.getprop(SybConstant::CS_CON_STATUS)
  end

  # This call returns TRUE if the connection has been marked DEAD.
  # You need to reconnect to the server.
  def connection_dead?
    cn_stat = @conn.getprop(SybConstant::CS_CON_STATUS)
    return (cn_stat & SybConstant::CS_CONSTAT_DEAD) != 0
  end
  alias dead? connection_dead?

  ##### Image/Text methods ##########
  
  def image_transize (size=nil)
    return @image_transize if( size.nil? )
    @image_transize = size if( size.kind_of?(Numeric) )
  end

  def image_log( lg = nil )
    return @image_log if( lg.nil? )
    if( lg == false ) then
      @image_log = false
    else
      @image_log = true
    end
  end

  # Sends IMAGE/TEXT data
  # iodesc	SybIODesc object ( SyBase data I/O descriptor)
  #		( This will be destructively UPDATED !)
  # imagesize	The whole size , in bytes, of Image/Text data to send.
  #             if imagesize equal 0 , then  update NULL .
  # sendproc    When setup to transmit data was completed, this
  #            method calls *sendproc*  repeatedly. In this block, you
  #            must provide the server with a chunk of Image/Text
  #            data.
  #             eg.   def foo( cmd){ }
  #            Must return String object to send,  or return nil as EOF.
  #
  # ERROR:     Raise RuntimeError 
  def send_image (iodesc, imagesize, &sendproc)
    cmd = nil
    begin
      # allocate CS_COMMAND and call ct_command()
      cmd = SybCommand.new( self.connection, nil, CS_SEND_DATA_CMD)
      # Change IODESC
      iohash = iodesc.props( { 'log_on_update'=>@image_log ,
			      'total_txtlen'=>imagesize })
      # Set new IODESC
      if( cmd.set_iodesc(iodesc) != CS_SUCCEED ) then
	raise "send_image: Failed set_iodesc()"
      end
      # print iodesc.inspect if( $DEBUG )

      # Send image
      sendsize = 0
      if( imagesize <= 0 ) then
	# Send zero length image data 
	if( cmd.send_data( nil ) != CS_SUCCEED ) then
	  raise "send_image: Failed send_data(nil)"
	end
      else
	until( (data = sendproc.call(cmd)).nil? )
	  sendsize += data.length
	  if( sendsize > imagesize ) then
	    raise "send_image: Overflow datasize "
	  end
	  # Call ct_send_data() API 
	  if( cmd.send_data( data ) != CS_SUCCEED ) then
	    raise "send_image: Failed send_data()"
	  end
	  print "#{sendsize} / #{imagesize} send\n" if($DEBUG)
	end
      end

      # Call ct_send() API, Flush NetWork buffer
      raise "send_image: Failed send()" unless( cmd.send)
      while( true )
	type = cmd.results()
	case type
	when nil
	  break
	when CS_PARAM_RESULT
	  (cols, rows) = cmd.fetchloop(0)
	  if( $DEBUG )then
	    # for debug
	    print cols.join('|'), "\n" if( cols.kind_of?(Array) )
	    if( rows.kind_of?(Array) ) then
	      rows.each {
		|r| print r.join('|'), "\n"
	      }
	    end
	  end
	when CS_CMD_SUCCEED,CS_CMD_DONE
	  next
	else
	  raise "send_image: SybCommand.results return Unexpected value (#{type})"
	end
      end
      return true
    rescue
      raise	# continue exception
    ensure
      if( cmd.kind_of?( SybCommand ) ) then
	print "cmd delete\n" if ( $DEBUG )
	cmd.delete()
      end
    end
  end

  # Sends contents of the file to a server as Image or Text data.
  #    The data transfer size is self.image_transize()
  # iodesc	SybIODesc Object( SyBase data I/O descriptor)
  #		( This will be destructively UPDATED !)
  #             If file-size 0.  then updates NULL
  # imagename	File name
  # ERROR:  Raise RuntimeError
  def send_imagefile (iodesc, imagename)
    file = nil
    begin
      imagesize = File.size(imagename)
      # raise "ImageSize is 0" if( imagesize <= 0 )
      file = File.open(imagename)
      tsize = self.image_transize()
      self.send_image(iodesc, imagesize){
	|cm|
	file.read( tsize )
      }
    ensure
      if( file.kind_of?(File) ) then
	print "File close\n" if ( $DEBUG )
	file.close
      end
    end
  end

  #  Gets the current SybIODesc object (I/O descriptor)
  # Functions
  #   (The sql method) + 
  #       ( The following of  *id* , fetch SybIODesc object )
  # sql		select statement to fetch 
  # id		The first SybIODesc column (start at 1)
  # eg.
  #  (id .. self.columns.length).each{|cid| self.top_row_result.nthrow(0,cid-1)}
  # ERROR:  RuntimeError 
  def sql_iodesc( sqlstr, id)
    iodesc = false
    raise SyntaxError, "id > 0" if( id <= 0 )
    # define update columns
    self.sql(sqlstr,id-1 ){
      # Process each one row
      |cm,s,clm,r|
      # If an Exception has raised in this block, Is the cmd.delete() effective
      # in self.sql() ENSURE  ?????
      # dummy get_data()
      (id .. clm.length).each {
	|imgid|
	(status, data) = cm.get_data(imgid,0)
	case status
	when CS_END_ITEM, CS_END_DATA,CS_SUCCEED
	  print "cm.get_data( #{imgid},0) return #{status}\n" if( $DEBUG )
	  iodesc = cm.get_iodesc(imgid)
	   unless( iodesc.kind_of?( SybIODesc ) ) then
	     raise "sql_iodesc: get_data(#{imgid}) fail"
	   end
	  r[ imgid - 1 ] = iodesc
	else
	  raise "update_image: cm.get_data(#{imgid}, 0) fail"
	end  # case END
      } # END of imgid loop
      true
    } # END of sql block 
    return true
  end

  # Retrieve Image or Text data.
  #    The data transfer size is self.image_transize()
  # receiver    Block that called when received data
  #             def foo(rowid,r clmid, clm, data)
  #             rowid,r:	RowNumber(start 0),RowDataArray
  #             clmid,clm:	ColumnNumber(start 0),ColumnArray
  #             data		String -- Received data
  #		                nil  -- End of Data Chunk
  #                             false -- ERROR 
  # sqlstr	SQL statement to fetch the row
  # id		The first Image/Text column (start at 1)
  # ERROR:  Raise RuntimeError 
  def sql_getimage( sqlstr, id, &receiver)
    raise SyntaxError, "id > 0" if( id <= 0 )
    rowid = 0
    # send sql command
    self.sql(sqlstr,id-1 ){
      # Process each one row
      |cm,s,clm,r|
      (id .. clm.length).each {
	|imgid|
	while( true )
	  (status, data) = cm.get_data(imgid,@image_transize)
	  if( data.kind_of?(String) && ( data.size > 0 ) ) then
	    # if status=CS_END_XXX and data is  String
	    receiver.call(rowid, r,  imgid-1, clm, data)
	  end
	  case status
	  when CS_END_ITEM, CS_END_DATA
	    receiver.call(rowid, r, imgid-1, clm, nil)
	    break
	  when CS_SUCCEED
	    next
	  else
	    receiver.call(rowid, r, imgid-1, clm, false)
	    break
	  end
	end  # End of get_data loop
      } # END of imgid loop
      rowid += 1
      true
    } # END of sql block 
    return true
  end

  def SybSQL::freetds?
    SybConstant::IS_FREETDS
  end
end
