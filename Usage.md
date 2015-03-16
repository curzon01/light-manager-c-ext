# HTTP requests #

## Apache + PHP ##

First be sure you have Apache and PHP installed and running (on Debian use `sudo apt-get install apache2 php`)

Create the following PHP script control.php where your www files are located (e.g. on Debian Apache2 it will be /var/www/):
```
<?php
	$cmd = $_GET['cmd'];	/* get cmd from url parameter */
	$addr = $_GET['addr'];
	if( $addr == "" ) {
		$addr="127.0.0.1";
	}
	$port = $_GET['port'];
	if( $port == "" ) {
		$port=3456;
	}

	$fp = fsockopen($addr, $port, $errno, $errstr);
	if ($fp) {
		// Read welcome string
		fread($fp, 64);
		// Write command
		fwrite($fp, $cmd);
		// End of command
		fwrite($fp, "\n");
		// Return result
		echo fread($fp, 16);
		// Finally read command prompt
		fread($fp, 8);
		
		fclose($fp);
	}
?>
```

### Examples ###
Get temperature from lightmanager running on server `raspi_ip` where it is listening on port 4444:<br>
<code>http://raspi_ip/control.php?addr=raspi_ip&amp;port=4444&amp;cmd=get temp</code>

Switch FS20 device 1133 to dim level 50%, close Uniroll jalousie 5 down for 10 sec:<br>
<code>http://raspi_ip/control.php?addr=raspi_ip&amp;port=4444&amp;cmd=fs20 1133 50%; uniroll 5 down; wait 10000; uniroll 5 stop</code>


<h2>Build in HTTP Server</h2>

Assuming you start the server listening on http port 80<br>
<pre><code>sudo ./lightmanager -d -s -h 11223344 -p 80<br>
</code></pre>
you can use direct http requests.<br>
<br>
<h3>Examples</h3>
Get temperature from lightmanager running on server <code>raspi_ip</code> where it is listening on port 80:<br>
<code>http://raspi_ip/cmd=get temp</code>

Switch FS20 device 1133 to dim level 50%, close Uniroll jalousie 5 down for 10 sec:<br>
<code>http://raspi_ip/cmd=fs20 1133 50%&amp;uniroll 5 down&amp;wait 10000&amp;uniroll 5 stop</code>

If your server is listening on another port than 80, add :port to the server address (e.g. like on previous examples the server is listening on port 4444):<br>
<code>http://raspi_ip:4444/cmd=help&amp;get temp&amp;get time</code>


<h1>Others</h1>

<h2>Set clock from command line</h2>

Set Light Manager device clock to same as server clock to lightmanager running on server <code>raspi_ip</code> where it is listening on port 4444:<br>
<code>echo "set time;quit" | nc rasp_ip 4444</code>

If you run a ntp client on the server where lightmanager runs, this can also be used as a cron job entry to set the time of the Light Manager e.g. daily.