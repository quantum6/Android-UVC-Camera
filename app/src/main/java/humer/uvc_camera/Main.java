package humer.uvc_camera;

// TODO:
// - Explizit einen isochronousen oder bulk Endpoint auswählen.
// - Alt-Interface automatisch suchen aufgrund von maxPacketSize und
// - Sauberen Close/Open programmieren. econ 5MP USB3 läuft nur nach re-open.

import android.app.Activity;
import android.app.PendingIntent;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.ImageFormat;
import android.graphics.Rect;
import android.graphics.YuvImage;
import android.hardware.usb.UsbConstants;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbDeviceConnection;
import android.hardware.usb.UsbEndpoint;
import android.hardware.usb.UsbInterface;
import android.hardware.usb.UsbManager;
import android.media.MediaPlayer;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.support.v7.widget.PopupMenu;
import android.util.Log;
import android.view.MenuItem;
import android.view.View;
import android.widget.Button;
import android.widget.ImageButton;
import android.widget.ImageView;
import android.widget.TextView;
import android.widget.Toast;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Date;
import java.util.HashMap;
import java.util.concurrent.Callable;


public class Main extends Activity {

    private static final String ACTION_USB_PERMISSION = "humer.uvc_camera.USB_PERMISSION";

    // USB codes:
// Request types (bmRequestType):
    private static final int RT_STANDARD_INTERFACE_SET = 0x01;
    private static final int RT_CLASS_INTERFACE_SET = 0x21;
    private static final int RT_CLASS_INTERFACE_GET = 0xA1;
    // Video interface subclass codes:
    private static final int SC_VIDEOCONTROL = 0x01;
    private static final int SC_VIDEOSTREAMING = 0x02;
    // Standard request codes:
    private static final int SET_INTERFACE = 0x0b;
    // Video class-specific request codes:
    private static final int SET_CUR = 0x01;
    private static final int GET_CUR = 0x81;
    // VideoControl interface control selectors (CS):
    private static final int VC_REQUEST_ERROR_CODE_CONTROL = 0x02;
    // VideoStreaming interface control selectors (CS):
    private static final int VS_PROBE_CONTROL = 0x01;
    private static final int VS_COMMIT_CONTROL = 0x02;
    private static final int VS_STILL_PROBE_CONTROL = 0x03;
    private static final int VS_STILL_COMMIT_CONTROL = 0x04;
    private static final int VS_STREAM_ERROR_CODE_CONTROL = 0x06;
    private static final int VS_STILL_IMAGE_TRIGGER_CONTROL = 0x05;


    private enum CameraType {arkmicro, arkmicro1, microdia, microdia_1, microdia_2, microdia2, microdia2_1, prehkeytec, prehkeytec_1, prehkeytec_2, prehkeytec_3, prehkeytec1, prehkeytec1_1, prehkeytec1_2, prehkeytec2, prehkeytec2_1, logitechC310, econ_5MP_USB2, econ_5MP_USB3, econ_8MP_USB3, delock, wellta}

    ;

    private UsbManager usbManager;
    private CameraType cameraType;
    private UsbDevice camDevice = null;
    private UsbDeviceConnection camDeviceConnection;
    private UsbInterface camControlInterface;
    private UsbInterface camStreamingInterface;
    public static int camStreamingAltSetting;
    private UsbEndpoint camStreamingEndpoint;
    public boolean bulkMode;

    public static int camFormatIndex;
    public static int camFrameIndex;
    public static int camFrameInterval;
    public static int packetsPerRequest;
    public static int maxPacketSize;
    public static int imageWidth;
    public static int imageHeight;
    public static int activeUrbs;
    public static int videoformat;

    private UsbIso usbIso;
    public static boolean camIsOpen;
    private boolean backgroundJobActive;
    public ImageView imageView;
    public Bitmap bmp = null;

    public boolean bildaufnahme = false;
    public int stopKamera = 0;
    public int kamera = 0;
    public int stillImageFrame = 0;
    public int stillImageFrameBeenden = 0;
    public boolean stillImageAufnahme = false;
    public int stillImage = 0;
    public static char kameramodel ='m';

    public int exit = 0;
    public int mjpegYuv = 0;

    public int frameZähler = 0;
    public Handler handler;
    public Button button1;
    public Button button2;
    public Button button3;
    ImageButton iB;
    TextView tv;
    String[] mStringArray;
    Date date;
    SimpleDateFormat dateFormat;
    File file;
    String rootPath;
    String fileName;




    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.layout_main);
        //imageView = (ImageView) findViewById(R.id.imageView);

        // Example of a call to a native method
        tv = (TextView) findViewById(R.id.textDarstellung);
        tv.setText("Hallo, Bitte Kamera anschließen");
        usbManager = (UsbManager) getSystemService(USB_SERVICE);
        handler = new Handler(); // This makes the handler attached to UI Thread
        //SaveToFile  stf = new SaveToFile(this);

    }

    public void Main(int i){


    }


    public void kameraEinstellungen ( View view) {


        runOnUiThread(new Runnable() {
            @Override
            public void run() {

                setContentView(R.layout.einstellungen);
                imageView = (ImageView) findViewById(R.id.imageView_prehkeytec);
                button1 = (Button) findViewById(R.id.button1);

            }
        });

    }


    public void kameraSuchenAktion(View view) {



        try {
            findCam();
        } catch (Exception e) {
            e.printStackTrace();
        }

        if (camDevice != null) {
            int a;
            PendingIntent permissionIntent = PendingIntent.getBroadcast(this, 0, new Intent(ACTION_USB_PERMISSION), 0);
            // IntentFilter filter = new IntentFilter(ACTION_USB_PERMISSION);
            // registerReceiver(mUsbReceiver, filter);
            usbManager.requestPermission(camDevice, permissionIntent);
            while (!usbManager.hasPermission(camDevice)) {
                long time0 = System.currentTimeMillis();
                for (a = 0; a < 10; a++) {
                    while (System.currentTimeMillis() - time0 < 1000) {
                        if (usbManager.hasPermission(camDevice)) break;
                    }
                }
                if (usbManager.hasPermission(camDevice)) break;
                if ( a >= 10) break;
            }

            if (usbManager.hasPermission(camDevice)) {
                cameraType = detectCameraType(camDevice);
                if (cameraType == null) {
                    runOnUiThread(new Runnable() {
                        @Override
                        public void run() {
                            tv.setText("Kameratyp nicht erkannt.");
                        }
                    });
                } else {
                    switch (cameraType) {
                        case prehkeytec:
                            runOnUiThread(new Runnable() {
                                @Override
                                public void run() {
                                    setContentView(R.layout.layout_prehkeytec);
                                    imageView = (ImageView) findViewById(R.id.imageView_prehkeytec);
                                    button1 = (Button) findViewById(R.id.button1);
                                    button1.setOnClickListener(new View.OnClickListener() {

                                        @Override
                                        public void onClick(View view) {
                                            //Creating the instance of PopupMenu
                                            PopupMenu popup = new PopupMenu(Main.this, button1);
                                            //Inflating the Popup using xml file
                                            popup.getMenuInflater().inflate(R.menu.popup_menu, popup.getMenu());

                                            //registering popup with OnMenuItemClickListener
                                            popup.setOnMenuItemClickListener(new PopupMenu.OnMenuItemClickListener() {
                                                public boolean onMenuItemClick(MenuItem item) {
                                                    // Toast.makeText(Main.this,"Auswahl von: " + item.getTitle(),Toast.LENGTH_SHORT).show();
                                                    return true;
                                                }
                                            });
                                            popup.show();//showing popup menu
                                        }
                                    });//closing the setOnClickListener method
                                    button2 = (Button) findViewById(R.id.button2);
                                    button2.setOnClickListener(new View.OnClickListener() {
                                        @Override
                                        public void onClick(View view) {
                                            //Creating the instance of PopupMenu
                                            PopupMenu popup = new PopupMenu(Main.this, button2);
                                            //Inflating the Popup using xml file
                                            popup.getMenuInflater().inflate(R.menu.popup_kamera_prehkeytec, popup.getMenu());

                                            //registering popup with OnMenuItemClickListener
                                            popup.setOnMenuItemClickListener(new PopupMenu.OnMenuItemClickListener() {
                                                public boolean onMenuItemClick(MenuItem item) {
                                                    //  Toast.makeText(Main.this,"Auswahl von: " + item.getTitle(),Toast.LENGTH_SHORT).show();
                                                    return true;
                                                }
                                            });
                                            popup.show();//showing popup menu
                                        }
                                    });//closing the setOnClickListener method
                                    button3 = (Button) findViewById(R.id.button3);
                                    button3.setOnClickListener(new View.OnClickListener() {
                                        @Override
                                        public void onClick(View view) {
                                            //Creating the instance of PopupMenu
                                            PopupMenu popup = new PopupMenu(Main.this, button3);
                                            //Inflating the Popup using xml file
                                            popup.getMenuInflater().inflate(R.menu.fehlersuche_microdia, popup.getMenu());

                                            //registering popup with OnMenuItemClickListener
                                            popup.setOnMenuItemClickListener(new PopupMenu.OnMenuItemClickListener() {
                                                public boolean onMenuItemClick(MenuItem item) {
                                                    //  Toast.makeText(Main.this,"Auswahl von: " + item.getTitle(),Toast.LENGTH_SHORT).show();
                                                    return true; }
                                            });
                                            popup.show();//showing popup menu
                                        }
                                    });//closing the setOnClickListener method
                                    iB = (ImageButton) findViewById(R.id.Bildaufnahme);
                                    final MediaPlayer mp2 = MediaPlayer.create(Main.this, R.raw.sound2);
                                    final MediaPlayer mp1 = MediaPlayer.create(Main.this, R.raw.sound1);
                                    iB.setOnLongClickListener(new View.OnLongClickListener() {
                                        @Override
                                        public boolean onLongClick(View view) {
                                            mp2.start();
                                            hoheAuflösung();
                                            return true;
                                        }
                                    });
                                    iB.setOnClickListener(new View.OnClickListener() {
                                        @Override
                                        public void onClick(View view) {
                                            mp1.start();
                                            BildaufnahmeButtonClickEvent();
                                        }
                                    });
                                }
                            });
                            break;

                        case arkmicro:
                            runOnUiThread(new Runnable() {
                                @Override
                                public void run() {
                                    setContentView(R.layout.layout_arkmicro);
                                }
                            });
                            break;

                        case microdia:
                            runOnUiThread(new Runnable() {
                                @Override
                                public void run() {
                                    setContentView(R.layout.layout_microdia);
                                    imageView = (ImageView) findViewById(R.id.imageView_microdia);
                                    button1 = (Button) findViewById(R.id.button1);
                                    button1.setOnClickListener(new View.OnClickListener() {

                                        @Override
                                        public void onClick(View view) {
                                            //Creating the instance of PopupMenu
                                            PopupMenu popup = new PopupMenu(Main.this, button1);
                                            //Inflating the Popup using xml file
                                            popup.getMenuInflater().inflate(R.menu.popup_menu, popup.getMenu());

                                            //registering popup with OnMenuItemClickListener
                                            popup.setOnMenuItemClickListener(new PopupMenu.OnMenuItemClickListener() {
                                                public boolean onMenuItemClick(MenuItem item) {
                                                    // Toast.makeText(Main.this,"Auswahl von: " + item.getTitle(),Toast.LENGTH_SHORT).show();
                                                    return true;
                                                }
                                            });
                                            popup.show();//showing popup menu
                                        }
                                    });//closing the setOnClickListener method
                                    button2 = (Button) findViewById(R.id.button2);
                                    button2.setOnClickListener(new View.OnClickListener() {
                                        @Override
                                        public void onClick(View view) {
                                            //Creating the instance of PopupMenu
                                            PopupMenu popup = new PopupMenu(Main.this, button2);
                                            //Inflating the Popup using xml file
                                            popup.getMenuInflater().inflate(R.menu.popup_kamera_microdia, popup.getMenu());

                                            //registering popup with OnMenuItemClickListener
                                            popup.setOnMenuItemClickListener(new PopupMenu.OnMenuItemClickListener() {
                                                public boolean onMenuItemClick(MenuItem item) {
                                                    //  Toast.makeText(Main.this,"Auswahl von: " + item.getTitle(),Toast.LENGTH_SHORT).show();
                                                    return true;
                                                }
                                            });
                                            popup.show();//showing popup menu
                                        }
                                    });//closing the setOnClickListener method
                                    button3 = (Button) findViewById(R.id.button3);
                                    button3.setOnClickListener(new View.OnClickListener() {
                                        @Override
                                        public void onClick(View view) {
                                            //Creating the instance of PopupMenu
                                            PopupMenu popup = new PopupMenu(Main.this, button3);
                                            //Inflating the Popup using xml file
                                            popup.getMenuInflater().inflate(R.menu.fehlersuche_microdia, popup.getMenu());

                                            //registering popup with OnMenuItemClickListener
                                            popup.setOnMenuItemClickListener(new PopupMenu.OnMenuItemClickListener() {
                                                public boolean onMenuItemClick(MenuItem item) {
                                                    //  Toast.makeText(Main.this,"Auswahl von: " + item.getTitle(),Toast.LENGTH_SHORT).show();
                                                    return true; }
                                            });
                                            popup.show();//showing popup menu
                                        }
                                    });//closing the setOnClickListener method
                                    iB = (ImageButton) findViewById(R.id.Bildaufnahme);
                                    final MediaPlayer mp2 = MediaPlayer.create(Main.this, R.raw.sound2);
                                    final MediaPlayer mp1 = MediaPlayer.create(Main.this, R.raw.sound1);
                                    iB.setOnLongClickListener(new View.OnLongClickListener() {
                                        @Override
                                        public boolean onLongClick(View view) {
                                            mp2.start();
                                            hoheAuflösung();
                                            return true;
                                        }
                                    });
                                    iB.setOnClickListener(new View.OnClickListener() {
                                        @Override
                                        public void onClick(View view) {
                                            mp1.start();
                                            BildaufnahmeButtonClickEvent();
                                        }
                                    });
                                }
                            });

                            break;

                        default:
                            runOnUiThread(new Runnable() {
                                @Override
                                public void run() {
                                    tv.setText("Kameratyp erkannt, aber keine Kamera passt.");
                                }
                            });
                            break;
                    }
                }

            } else {
                runOnUiThread(new Runnable() {
                    @Override
                    public void run() {
                        tv.setText("Es wurden keine Berechtigungen für die Kamera erteilt.");
                    }
                });
            }

        } else {
            runOnUiThread(new Runnable() {
                @Override
                public void run() {
                    tv.setText("Keine Kamera erkannt.");
                }
            });
        }
    }







    public void kameraMicrodia_0(MenuItem item) {   kamera =0 ;  }
    public void kameraMicrodia_1(MenuItem item) {  kamera =1 ;   }
    public void kameraMicrodia_2(MenuItem item) {   kamera =2 ;  }
    public void kameraMicrodia_3(MenuItem item) {   kamera =3 ; }
    public void kameraMicrodia_4(MenuItem item) {   kamera =4 ; }
    public void kameraMicrodia_5(MenuItem item) {   kamera =5 ; }

    public void kameraPrehkeytec_0(MenuItem item) {   kamera =0 ; }
    public void kameraPrehkeytec_1(MenuItem item) {   kamera =1 ; }
    public void kameraPrehkeytec_2(MenuItem item) {   kamera =2 ; }
    public void kameraPrehkeytec_3(MenuItem item) {   kamera =3 ; }
    public void kameraPrehkeytec_4(MenuItem item) {   kamera =4 ; }
    public void kameraPrehkeytec_5(MenuItem item) {   kamera =5 ; }




    public void xGo_1920x1080(MenuItem item) {
        kamera = 4;
    }
    public void xGo_1280x1024(MenuItem item) { kamera = 5; }
    public void xGo_1280x780(MenuItem item) {
        kamera = 6;
    }


    public void active_1280x780(MenuItem item) {
        kamera = 7;
    }
    public void active_16_16(MenuItem item) {
        kamera = 8;
    }

    public void beenden(MenuItem item) {
        camDeviceConnection.releaseInterface(camControlInterface);
        camDeviceConnection.releaseInterface(camStreamingInterface);
        camDeviceConnection.close();
        finish();
    }


    public void findCamButtonClickEvent(MenuItem item) {
        try {
            findCam();
        } catch (Exception e) {
            displayErrorMessage(e);
            return;
        }
        displayMessage("USB camera found: " + camDevice.getDeviceName());
        // listDeviceInterfaces(camDevice);
    }

    public void findCamButtonClickEvent(View view) {
        try {
            findCam();
        } catch (Exception e) {
            displayErrorMessage(e);
            return;
        }
        displayMessage("USB camera found: " + camDevice.getDeviceName());
        // listDeviceInterfaces(camDevice);
    }

    private void listDevice(UsbDevice usbDevice) {
        log("Interface count: " + usbDevice.getInterfaceCount());
        int interfaces = usbDevice.getInterfaceCount();

        ArrayList<String> logArray = new ArrayList<String>(512);

        for (int i = 0; i < interfaces; i++) {

            UsbInterface usbInterface = usbDevice.getInterface(i);
            log("-Interface " + i + ": id=" + usbInterface.getId() + " class=" + usbInterface.getInterfaceClass() + " subclass=" + usbInterface.getInterfaceSubclass() + " protocol=" + usbInterface.getInterfaceProtocol());
            // UsbInterface.getAlternateSetting() has been added in Android 5.
            int endpoints = usbInterface.getEndpointCount();
            StringBuilder logEntry = new StringBuilder("/ InterfaceID " + usbInterface.getId() + "/ Interfaceclass " + usbInterface.getInterfaceClass() + " / protocol=" + usbInterface.getInterfaceProtocol());
            logArray.add(logEntry.toString());

            for (int j = 0; j < endpoints; j++) {
                UsbEndpoint usbEndpoint = usbInterface.getEndpoint(j);
                log("- Endpoint " + j + ": addr=" + usbEndpoint.getAddress() + " [direction=" + usbEndpoint.getDirection() + " endpointNumber=" + usbEndpoint.getEndpointNumber() + "] " +
                        " attrs=" + usbEndpoint.getAttributes() + " interval=" + usbEndpoint.getInterval() + " maxPacketSize=" + usbEndpoint.getMaxPacketSize() + " type=" + usbEndpoint.getType());

                StringBuilder logEntry2 = new StringBuilder("Endpoint " + j + "/ addr 0x" + Integer.toHexString(usbEndpoint.getAddress()) + "/ direction " + usbEndpoint.getDirection() + " / endpointNumber=" + usbEndpoint.getEndpointNumber() +  "] " + " attrs=" + usbEndpoint.getAttributes() + " interval=" + usbEndpoint.getInterval() + " maxPacketSize=" + usbEndpoint.getMaxPacketSize() + " type=" + usbEndpoint.getType() );
                logArray.add(logEntry2.toString());
            }
        }

        mStringArray = new String[logArray.size()];
        mStringArray = logArray.toArray(mStringArray);
        log("logArray.size() =" + logArray.size() );

        if (logArray.size() >= 15) {
            log("logArray.size() >= 15");
            runOnUiThread(new Runnable() {
                @Override
                public void run() {
                    setContentView(R.layout.layout_main);
                    tv = (TextView) findViewById(R.id.textDarstellung);
                    tv.setSingleLine(false);
                    tv.setText(mStringArray[0] + "\n" + mStringArray[1] + "\n" + mStringArray[2] + "\n" + mStringArray[3] + "\n" + mStringArray[4] + "\n" + mStringArray[5] + "\n" + mStringArray[6] + "\n" + mStringArray[7] + "\n" + mStringArray[8] + "\n" + mStringArray[9]+"\n" + mStringArray[10]+"\n" + mStringArray[11]+"\n" + mStringArray[12]+ "\n" +mStringArray[13]+ "\n" +mStringArray[14]);
                }
            });
        } else if (logArray.size() >= 10){
            log("logArray.size() >= 10");
            runOnUiThread(new Runnable() {
                @Override
                public void run() {
                    setContentView(R.layout.layout_main);
                    tv = (TextView) findViewById(R.id.textDarstellung);
                    tv.setSingleLine(false);
                    tv.setText(mStringArray[0] +"\n"+ mStringArray[1]+"\n"+ mStringArray[2]+"\n"+ mStringArray[3]+"\n"+ mStringArray[4]+"\n"+ mStringArray[5]+"\n"+ mStringArray[6]+"\n"+ mStringArray[7]+"\n"+ mStringArray[8]+"\n"+ mStringArray[9]);
                }
            });
        } else if (logArray.size() >= 5){
            log("logArray.size() >= 5");
            runOnUiThread(new Runnable() {
                @Override
                public void run() {
                    setContentView(R.layout.layout_main);
                    tv.setSingleLine(false);
                    tv.setText(mStringArray[0] +"\n"+ mStringArray[1]+"\n"+ mStringArray[2]+"\n"+ mStringArray[3]+"\n"+ mStringArray[4]);
                }
            });
        }


    }

    private void findCam() throws Exception {
        camDevice = findCameraDevice();
        if (camDevice == null) {
            throw new Exception("No USB camera device found.");
        }
    }

    private UsbDevice findCameraDevice() {
        HashMap<String, UsbDevice> deviceList = usbManager.getDeviceList();
        log("USB devices count = " + deviceList.size());
        for (UsbDevice usbDevice : deviceList.values()) {
            log("USB device \"" + usbDevice.getDeviceName() + "\": " + usbDevice);
            if (checkDeviceHasVideoStreamingInterface(usbDevice)) {
                return usbDevice;
            }
        }
        return null;
    }

    private boolean checkDeviceHasVideoStreamingInterface(UsbDevice usbDevice) {
        return getVideoStreamingInterface(usbDevice) != null;
    }

    private UsbInterface getVideoControlInterface(UsbDevice usbDevice) {
        return findInterface(usbDevice, UsbConstants.USB_CLASS_VIDEO, SC_VIDEOCONTROL, false);
    }

    private UsbInterface getVideoStreamingInterface(UsbDevice usbDevice) {
        return findInterface(usbDevice, UsbConstants.USB_CLASS_VIDEO, SC_VIDEOSTREAMING, true);
    }

    private UsbInterface findInterface(UsbDevice usbDevice, int interfaceClass, int interfaceSubclass, boolean withEndpoint) {
        int interfaces = usbDevice.getInterfaceCount();
        for (int i = 0; i < interfaces; i++) {
            UsbInterface usbInterface = usbDevice.getInterface(i);
            if (usbInterface.getInterfaceClass() == interfaceClass && usbInterface.getInterfaceSubclass() == interfaceSubclass && (!withEndpoint || usbInterface.getEndpointCount() > 0)) {
                return usbInterface;
            }
        }
        return null;
    }



    public void requestPermissionButtonClickEvent(MenuItem item) {
        try {
            findCam();
        } catch (Exception e) {
            displayErrorMessage(e);
            return;
        }
        PendingIntent permissionIntent = PendingIntent.getBroadcast(this, 0, new Intent(ACTION_USB_PERMISSION), 0);
        // IntentFilter filter = new IntentFilter(ACTION_USB_PERMISSION);
        // registerReceiver(mUsbReceiver, filter);
        usbManager.requestPermission(camDevice, permissionIntent);
    }


    public void listDeviceButtonClickEvent(MenuItem item) {
        try {
            findCam();
            listDevice(camDevice);
        } catch (Exception e) {
            displayErrorMessage(e);
            return;
        }
    }


    public void openCamButtonClickEvent(MenuItem item) {
        try {
            closeCameraDevice();
        } catch (Exception e) {
            displayErrorMessage(e);
            return;
        }
        try {
            openCam();
        } catch (Exception e) {
            displayErrorMessage(e);
            return;
        }
        // log("streamingInterfaceId=" + camStreamingInterface.getId() + " streamingEndpointAddr=0x" + Integer.toHexString(camStreamingEndpoint.getAddress()));
        displayMessage("OK");
    }


    private void openCam() throws Exception {
        findCam();
        openCameraDevice();
        initCamera();
        camIsOpen = true;
    }

    private void openCameraDevice() throws Exception {
        if (!usbManager.hasPermission(camDevice)) {
            throw new Exception("Permission missing for camera device.");
        }
        cameraType = detectCameraType(camDevice);
        if (cameraType == null) {
            throw new Exception("Camera type not recognized.");
        }
        // (For transfer buffer sizes > 196608 the kernel file drivers/usb/core/devio.c must be patched.)
        switch (cameraType) {                           // temporary solution
            case prehkeytec: // für Acer

                Prehkeytec prehkeytek = new Prehkeytec();
                prehkeytek.kameraEinstellungen(kamera, mjpegYuv);

                break;

            case prehkeytec2_1:    // für Xperia Active
                camStreamingAltSetting = 1;              // 1 = 3x1024 bytes packet size
                maxPacketSize = 3 * 1024;
                //camStreamingAltSetting = 2;              // 2 = 2x 1024 bytes packet size
                //maxPacketSize = 2* 1024;
                //camStreamingAltSetting = 3;              // 3 = 1x 1024 bytes packet size
                //maxPacketSize = 1* 1024;
                //camStreamingAltSetting = 4;              // 4 = 1x 512 bytes packet size
                //maxPacketSize = 512;
                camFormatIndex = 1;                       // bFormatIndex: 1 = MJPEG
                camFrameIndex = 3;                        // bFrameIndex: 1 = 1920 x 1080, 2 = 1280 x 1024, 3 = 1280 x 720
                camFrameInterval = 2000000;               // dwFrameInterval: 333333 =  30 fps // 400000 = 25 fps // 500000 = 20 fps   // 666666 = 15 fps // 1000000 = 10 fps   2000000 = 5 fps
                packetsPerRequest = 4;             //4 für Acer A3A20
                activeUrbs = 4;                     //4 für Acer A3A20
                kameramodel = 'p';
                break;


            case arkmicro:
                camStreamingAltSetting = 11;              // 11 = 3x1000 bytes packet size // 6 = 1x 960 bytes   5 = 1x 800 bytes
                maxPacketSize = 3 * 1000;

                //camStreamingAltSetting = 9;              // 9 = 2x 992 bytes packet size
                //maxPacketSize = 2* 992;

                //camStreamingAltSetting = 1;              // 1 = 1x 192 bytes packet size
                //maxPacketSize = 1 * 192;

                //camStreamingAltSetting = 2;              // 1 = 1x 384 bytes packet size
                //maxPacketSize = 1* 384;

                //camStreamingAltSetting = 7;              // 7 = 2x 640 bytes packet size
                //maxPacketSize = 2* 640;

                //camStreamingAltSetting = 4;              // 4 = 1x 640 bytes packet size
                //maxPacketSize = 1* 640;
                camFormatIndex = 1;                       // bFormatIndex: 1 = uncompressed YUY2
                camFrameIndex = 1;                        // bFrameIndex: 1 = 640 x 480, 2 = 160 x 120, 3 = 176 x 144, 4 = 320 x 240, 5 = 352 x 288
                imageWidth = 640;
                imageHeight = 480;
                camFrameInterval = 2000000;               // dwFrameInterval: 333333 =  30 fps   // 666666 = 15 fps // 1000000 = 10 fps   2000000 = 5 fps
                // camFrameInterval = 2000000;
                packetsPerRequest = 4;
                activeUrbs = 4;
                break;
            case arkmicro1:
                camStreamingAltSetting = 11;              // 7 = 2x 640 bytes packet size
                maxPacketSize = 3* 1000;
                camFormatIndex = 1;                       // bFormatIndex: 1 = uncompressed YUY2
                camFrameIndex = 2;                        // bFrameIndex: 1 = 640 x 480, 2 = 160 x 120, 3 = 176 x 144, 4 = 320 x 240, 5 = 352 x 288
                imageWidth = 160;
                imageHeight = 120;
                camFrameInterval = 2000000;               // dwFrameInterval: 333333 =  30 fps   // 666666 = 15 fps // 1000000 = 10 fps   2000000 = 5 fps
                // camFrameInterval = 2000000;
                packetsPerRequest = 4;
                activeUrbs = 4;
                break;


            case microdia:

                Microdia microdia = new Microdia();
                microdia.kameraEinstellungen(kamera, mjpegYuv);

                break;


            default:
                throw new AssertionError();
        }
        camControlInterface = getVideoControlInterface(camDevice);
        camStreamingInterface = getVideoStreamingInterface(camDevice);
        if (camStreamingInterface.getEndpointCount() < 1) {
            throw new Exception("Streaming interface has no endpoint.");
        }
        camStreamingEndpoint = camStreamingInterface.getEndpoint(0);
        bulkMode = camStreamingEndpoint.getType() == UsbConstants.USB_ENDPOINT_XFER_BULK;
        camDeviceConnection = usbManager.openDevice(camDevice);
        if (camDeviceConnection == null) {
            throw new Exception("Unable to open camera device connection.");
        }
        if (!camDeviceConnection.claimInterface(camControlInterface, true)) {
            throw new Exception("Unable to claim camera control interface.");
        }
        if (!camDeviceConnection.claimInterface(camStreamingInterface, true)) {
            throw new Exception("Unable to claim camera streaming interface.");
        }
        usbIso = new UsbIso(camDeviceConnection.getFileDescriptor(), packetsPerRequest, maxPacketSize);
        usbIso.preallocateRequests(activeUrbs);
    }


    private CameraType detectCameraType(UsbDevice dev) {

        if (dev.getVendorId() == 0x0c45 && dev.getProductId() == 0x6366) {
            return CameraType.microdia;
        } else if (dev.getVendorId() == 0x18ec && dev.getProductId() == 0x3390) {
            return CameraType.arkmicro;
        } else if (dev.getVendorId() == 0x053a && dev.getProductId() == 0x9230) {
            return CameraType.prehkeytec;
        } else {
            return null;
        }
    }
    private void closeCameraDevice() throws IOException {
        if (usbIso != null) {
            usbIso.dispose();
            usbIso = null;
        }
        if (camDeviceConnection != null) {
            camDeviceConnection.releaseInterface(camControlInterface);
            camDeviceConnection.releaseInterface(camStreamingInterface);
            camDeviceConnection.close();
            camDeviceConnection = null;
        }
    }

    private void initCamera() throws Exception {
        try {
            getVideoControlErrorCode();
        }                // to reset previous error states
        catch (Exception e) {
            log("Warning: getVideoControlErrorCode() failed: " + e);
        }   // ignore error, some cameras do not support the request
        enableStreaming(false);
        try {
            getVideoStreamErrorCode();
        }                // to reset previous error states
        catch (Exception e) {
            log("Warning: getVideoStreamErrorCode() failed: " + e);
        }   // ignore error, some cameras do not support the request
        initStreamingParms();

        if (cameraType == CameraType.prehkeytec_1) {
            initStillImageParms();
        }
        if (cameraType == CameraType.prehkeytec_2) {
            initStillImageParms();
        }
        if (cameraType == CameraType.prehkeytec) {
            initStillImageParms();
        }


        if (cameraType == CameraType.prehkeytec1_1) {
            initStillImageParms();
        }
        if (cameraType == CameraType.prehkeytec1_2) {
            initStillImageParms();
        }
        if (cameraType == CameraType.prehkeytec1) {
            initStillImageParms();
        }


        //if (cameraType == CameraType.arkmicro) {
        //    initStillImageParms(); }
        //if (cameraType == CameraType.microdia) {
        //    initStillImageParms(); }
        //...
    }


    public void test1ButtonClickEvent(MenuItem Item) {
        try {
            if (!camIsOpen) {
                openCam();
            }
            startBackgroundJob(new Callable<Void>() {
                @Override
                public Void call() throws Exception {
                    if (bulkMode) {
                        //testBulkRead1();
                        // testBulkRead2();
                        // testBulkRead3();
                        // testBulkRead4();
                    } else {
                        testIsochronousRead1();
                    }
                    return null;
                }
            });
        } catch (Exception e) {
            displayErrorMessage(e);
            return;
        }
        displayMessage("OK");
    }


    public void test2ButtonClickEvent(MenuItem Item) {
        try {
            if (!camIsOpen) {
                openCam();
            }
            startBackgroundJob(new Callable<Void>() {
                @Override
                public Void call() throws Exception {
                    if (bulkMode) {
                        // testBulkRead1();
                        // testBulkRead2();
                        // testBulkRead3();
                        // testBulkRead4();
                    } else {
                        testIsochronousRead2();
                    }
                    return null;
                }
            });
        } catch (Exception e) {
            displayErrorMessage(e);
            return;
        }
        displayMessage("OK");
    }



    public void StartKameraButtonClickEvent(MenuItem Item) {
        if (stopKamera == 1){
            displayMessage("Übertragung auf Stopp");
        } else {

            try {
                closeCameraDevice();
            } catch (Exception e) {
                displayErrorMessage(e);
                return;
            }
            try {
                openCam();
            } catch (Exception e) {
                displayErrorMessage(e);
                return;
            }

            try {
                if (!camIsOpen) {
                    openCam();
                }
                startBackgroundJob(new Callable<Void>() {
                    @Override
                    public Void call() throws Exception {
                        if (bulkMode) {
                            // testBulkRead1();
                            // testBulkRead2();
                            //testBulkRead3();
                            // testBulkRead4();
                        } else {
                            StartKamera();
                        }
                        return null;
                    }
                });
            } catch (Exception e) {
                displayErrorMessage(e);
                return;
            }
            displayMessage("OK");
            //surfaceView = (SurfaceView) findViewById(R.id.surfaceView);
            // surfaceHolder = surfaceView.getHolder();
            // surfaceHolder.addCallback(this);
            // surfaceHolder.setType(SurfaceHolder.SURFACE_TYPE_PUSH_BUFFERS);
        }
    }



    public void BildaufnahmeButtonClickEvent() {

        bildaufnahme = true;
        displayMessage("Bild gespeichert");
        log("Bildaufnahme");

    }

    public void hoheAuflösung() {

        stillImageFrame++;
        displayMessage("Bild gespeichert");
        log("Bildaufnahme");
    }




    public void StopKameraButtonClickEvent(View view) {


        if (stopKamera > 0) {
            try {
                enableStreaming(false);
                initCamera();
                enableStreaming(true);
            } catch (Exception e) {
                e.printStackTrace();
            }
            displayMessage("Übertragung startklar ");
            log("Übertragung startklar");
            stopKamera = 0;
        } else if (stopKamera < 1) {
            displayMessage("Gestoppt ");
            log("Gestoppt");
            stopKamera = 1;
        }

    }

    public void MjpegYuv(View view) {
        if (mjpegYuv == 0) {
            displayMessage("YUV");
            log("YUV");
            mjpegYuv = 1;
        } else {
            displayMessage("MJPEG ");
            log("MJPEG");
            mjpegYuv = 0;
        }

    }

    private void testIsochronousRead1() throws Exception {
        //Thread.sleep(500);
        ArrayList<String> logArray = new ArrayList<String>(512);
        int packetCnt = 0;
        int packet0Cnt = 0;
        int packet12Cnt = 0;
        int packetDataCnt = 0;
        int packetHdr8Ccnt = 0;
        int packetErrorCnt = 0;
        int frameCnt = 0;
        long time0 = System.currentTimeMillis();
        int frameLen = 0;
        int requestCnt = 0;
        byte[] data = new byte[maxPacketSize];
        enableStreaming(true);
        submitActiveUrbs();
        while (System.currentTimeMillis() - time0 < 10000) {
            // Thread.sleep(0, 1);               // ??????????
            boolean stopReq = false;
            UsbIso.Request req = usbIso.reapRequest(true);
            for (int packetNo = 0; packetNo < req.getPacketCount(); packetNo++) {
                packetCnt++;
                int packetLen = req.getPacketActualLength(packetNo);
                if (packetLen == 0) {
                    packet0Cnt++;
                }
                if (packetLen == 12) {
                    packet12Cnt++;
                }
                if (packetLen == 0) {
                    continue;
                }
                StringBuilder logEntry = new StringBuilder(requestCnt + "/" + packetNo + " len=" + packetLen);
                int packetStatus = req.getPacketStatus(packetNo);
                if (packetStatus != 0) {
                    log("Packet status=" + packetStatus);
                    stopReq = true;
                    break;
                }
                if (packetLen > 0) {
                    if (packetLen > maxPacketSize) {
                        throw new Exception("packetLen > maxPacketSize");
                    }
                    req.getPacketData(packetNo, data, packetLen);
                    logEntry.append(" data=" + hexDump(data, Math.min(32, packetLen)));
                    int headerLen = data[0] & 0xff;

                    try { if (headerLen < 2 || headerLen > packetLen) {
                    //    skipFrames = 1;
                    }
                    } catch (Exception e) {
                        log("Invalid payload header length.");
                    }


                    //if (headerLen < 2 || headerLen > packetLen) {
                    //    throw new IOException("Invalid payload header length. headerLen=" + headerLen + " packetLen=" + packetLen);
                    //}
                    int headerFlags = data[1] & 0xff;
                    if (headerFlags == 0x8c) {
                        packetHdr8Ccnt++;
                    }
                    // logEntry.append(" hdrLen=" + headerLen + " hdr[1]=0x" + Integer.toHexString(headerFlags));
                    int dataLen = packetLen - headerLen;
                    if (dataLen > 0) {
                        packetDataCnt++;
                    }
                    frameLen += dataLen;
                    if ((headerFlags & 0x40) != 0) {
                        logEntry.append(" *** Error ***");
                        packetErrorCnt++;
                    }
                    if ((headerFlags & 2) != 0) {
                        logEntry.append(" EOF frameLen=" + frameLen);
                        frameCnt++;
                        if (frameCnt == 10) {
                            sendStillImageTrigger(); }           // test ***********
                        frameLen = 0;
                    }
                }
                //if (packetLen == 0 && frameLen > 0) {
                //   logEntry.append(" assumed EOF, framelen=" + frameLen);
                //   frameLen = 0; }
                //int streamErrorCode = getVideoStreamErrorCode();
                //if (streamErrorCode != 0) {
                //   logEntry.append(" streamErrorCode=" + streamErrorCode); }
                //int controlErrorCode = getVideoControlErrorCode();
                // if (controlErrorCode != 0) {
                //  logEntry.append(" controlErrorCode=" + controlErrorCode); }
                logArray.add(logEntry.toString());
            }
            if (stopReq) {
                break;
            }
            if (stopKamera > 0) {
                break;
            }
            requestCnt++;
            req.initialize(camStreamingEndpoint.getAddress());
            req.submit();
        }
        try {
            enableStreaming(false);
        } catch (Exception e) {
            log("Exception during enableStreaming(false): " + e);
        }
        log("requests=" + requestCnt + " packetCnt=" + packetCnt + " packetErrorCnt=" + packetErrorCnt + " packet0Cnt=" + packet0Cnt + ", packet12Cnt=" + packet12Cnt + ", packetDataCnt=" + packetDataCnt + " packetHdr8cCnt=" + packetHdr8Ccnt + " frameCnt=" + frameCnt);
        for (String s : logArray) {
            log(s);
        }
    }

    private void testIsochronousRead2() throws Exception {
        // sendStillImageTrigger();            // test ***********
        // Thread.sleep(500);
        ArrayList<String> logArray = new ArrayList<String>(512);
        ByteArrayOutputStream frameData = new ByteArrayOutputStream(0x20000);
        long startTime = System.currentTimeMillis();
        int skipFrames = 0;
        // if (cameraType == CameraType.wellta) {
        //    skipFrames = 1; }                                // first frame may look intact but it is not always intact
        boolean frameComplete = false;
        byte[] data = new byte[maxPacketSize];
        enableStreaming(true);
        submitActiveUrbs();
        while (true) {
            // Thread.sleep(0, 100);               // ??????????
            if (System.currentTimeMillis() - startTime > 10000) {
                enableStreaming(false);
                for (String s : logArray) {
                    log(s);
                }
                throw new Exception("Timeout while waiting for image frame.");
            }
            UsbIso.Request req = usbIso.reapRequest(true);
            for (int packetNo = 0; packetNo < req.getPacketCount(); packetNo++) {
                int packetStatus = req.getPacketStatus(packetNo);
                try {if (packetStatus != 0) {
                    skipFrames = 1; }
                } catch  (Exception e) {
                    log("Camera read error, packet status=" + packetStatus);
                }
                int packetLen = req.getPacketActualLength(packetNo);
                if (packetLen == 0) {
                    // if (packetLen == 0 && frameData.size() > 0) {         // assume end of frame
                    //   endOfFrame = true;
                    //   break; }
                    continue;
                }
                if (packetLen > maxPacketSize) {
                    throw new Exception("packetLen > maxPacketSize");
                }
                req.getPacketData(packetNo, data, packetLen);
                int headerLen = data[0] & 0xff;

                try { if (headerLen < 2 || headerLen > packetLen) {
                    skipFrames = 1; }
                } catch (Exception e) {
                    log("Invalid payload header length.");
                }
                int headerFlags = data[1] & 0xff;
                int dataLen = packetLen - headerLen;
                boolean error = (headerFlags & 0x40) != 0;
                if (error && skipFrames == 0) {
                    // throw new IOException("Error flag set in payload header.");
                    log("Error flag detected, ignoring frame.");
                    skipFrames = 1;
                }
                boolean endOfFrame = (headerFlags & 2) != 0;
                if (dataLen > 0 && skipFrames == 0) {
                    frameData.write(data, headerLen, dataLen);
                }
                //
                // StringBuilder logEntry = new StringBuilder("packet " + packetNo + " len=" + packetLen);
                // if (dataLen > 0) logEntry.append(" data=" + hexDump(data, Math.min(32, packetLen)));
                // if (endOfFrame) logEntry.append(" EOF");
                // if (error) logEntry.append(" **Error**");
                // logArray.add(logEntry.toString());
                // final int frameDataSize = imageWidth * imageHeight * 2;
                // if (frameData.size() >= frameDataSize) {
                //    endOfFrame = true; }    // temp test
                if (endOfFrame) {
                    if (skipFrames > 0) {
                        log("Skipping frame, len= " + frameData.size());
                        frameData.reset();
                        skipFrames--;
                    } else {
                        frameComplete = true;
                        break;
                    }
                }
            }
            if (frameComplete) {
                break;
            }
            req.initialize(camStreamingEndpoint.getAddress());
            req.submit();

            if (stopKamera > 0) {
                break;
            }
        }
        enableStreaming(false);
        log("frame data len = " + frameData.size());
        if (mjpegYuv == 0) {
            processReceivedMJpegVideoFrameKamera(frameData.toByteArray());
        }else {
            processReceivedVideoFrameYuv(frameData.toByteArray());
        }
        //processReceivedMJpegVideoFrame(frameData.toByteArray());
        //saveReceivedVideoFrame(frameData.toByteArray());
        log("OK");
    }

    private void StartKamera() throws Exception {



        ByteArrayOutputStream frameData = new ByteArrayOutputStream(0x20000);
        long startTime = System.currentTimeMillis();
        int skipFrames = 0;
        // if (cameraType == CameraType.wellta) {
        //    skipFrames = 1; }                                // first frame may look intact but it is not always intact
        boolean frameComplete = false;
        byte[] data = new byte[maxPacketSize];
        enableStreaming(true);
        submitActiveUrbs();
        while (true) {
            if (System.currentTimeMillis() - startTime > 600000)
                 {
                 log ("Kamera läuft seit 10 Minuten");
                 }
            UsbIso.Request req = usbIso.reapRequest(true);
            for (int packetNo = 0; packetNo < req.getPacketCount(); packetNo++) {
                int packetStatus = req.getPacketStatus(packetNo);
                try {if (packetStatus != 0) {
                    skipFrames = 1;}

                    //    throw new IOException("Camera read error, packet status=" + packetStatus);
                    } catch (Exception e){
                    log("Camera read error, packet status=" + packetStatus);
                }
                int packetLen = req.getPacketActualLength(packetNo);
                if (packetLen == 0) {
                    // if (packetLen == 0 && frameData.size() > 0) {         // assume end of frame
                    //   endOfFrame = true;
                    //   break; }
                    continue;
                }
                if (packetLen > maxPacketSize) {
                    throw new Exception("packetLen > maxPacketSize");
                }
                req.getPacketData(packetNo, data, packetLen);
                int headerLen = data[0] & 0xff;

                try { if (headerLen < 2 || headerLen > packetLen) {
                    skipFrames = 1;
                }
                } catch (Exception e) {
                    log("Invalid payload header length.");
                }

               // if (headerLen < 2 || headerLen > packetLen) {
               //     throw new IOException("Invalid payload header length.");
               // }

                int headerFlags = data[1] & 0xff;
                int dataLen = packetLen - headerLen;
                boolean error = (headerFlags & 0x40) != 0;
                if (error && skipFrames == 0) {
                    // throw new IOException("Error flag set in payload header.");
//                    log("Error flag detected, ignoring frame.");
                    skipFrames = 1;


                }
                if (dataLen > 0 && skipFrames == 0) {
                    frameData.write(data, headerLen, dataLen);
                }
                if ((headerFlags & 2) != 0) {
                    if (skipFrames > 0) {
                        log("Skipping frame, len= " + frameData.size());
                        frameData.reset();
                        skipFrames--;
                    }
                    else {
                        if (stillImageFrame > stillImageFrameBeenden ) {
                            sendStillImageTrigger();
                            stillImageAufnahme = true;
                        }

                        stillImageFrameBeenden = stillImageFrame;
                        frameData.write(data, headerLen, dataLen);

                        if (mjpegYuv == 0) {
                            processReceivedMJpegVideoFrameKamera(frameData.toByteArray());
                        }else {
                            processReceivedVideoFrameYuv(frameData.toByteArray());
                        }
                        frameData.reset();


                    }


                }
            }


            req.initialize(camStreamingEndpoint.getAddress());
            req.submit();

            if (stopKamera > 0) {
                break;

            }
        }
        //enableStreaming(false);
        //processReceivedMJpegVideoFrame(frameData.toByteArray());
        //saveReceivedVideoFrame(frameData.toByteArray());
        log("OK");

    }


    private void submitActiveUrbs() throws IOException {
        for (int i = 0; i < activeUrbs; i++) {
            UsbIso.Request req = usbIso.getRequest();
            req.initialize(camStreamingEndpoint.getAddress());
            req.submit();
        }
        // log("Time used for submitActiveUrbs: " + (System.currentTimeMillis() - time0) + "ms.");
    }

    private void saveReceivedVideoFrame(byte[] frameData) throws Exception {
        File file = new File(Environment.getExternalStorageDirectory(), "temp_usbcamtest1.bin");
        FileOutputStream fileOutputStream = null;
        try {
            fileOutputStream = new FileOutputStream(file);
            fileOutputStream.write(frameData);
            fileOutputStream.flush();
        } finally {
            fileOutputStream.close();
        }
    }

    private void processReceivedVideoFrame1(byte[] frameData) throws IOException {
        String fileName = new File(Environment.getExternalStorageDirectory(), "temp_usbcamtest1.frame").getPath();
        writeBytesToFile(fileName, frameData);
    }

    private void writeBytesToFile(String fileName, byte[] data) throws IOException {
        FileOutputStream fileOutputStream = null;
        try {
            fileOutputStream = new FileOutputStream(fileName);
            fileOutputStream.write(data);
            fileOutputStream.flush();
        } finally {
            fileOutputStream.close();
        }
    }

    private void processReceivedVideoFrameYuv(byte[] frameData) throws IOException {
        log("before YuvImage");
        YuvImage yuvImage = new YuvImage(frameData, ImageFormat.YUY2, imageWidth, imageHeight, null);
        log("after YuvImage");



        Date date = new Date();
        String rootPath = Environment.getExternalStorageDirectory().getAbsolutePath() + "/ArkMikro/";
        String fileName = new File(rootPath + String.valueOf(date.getTime()) + ".jpg").getPath();
        File file = new File(rootPath, fileName);
        if (!file.exists()) {
            file.mkdirs();
        }
     //   writeBytesToFile(fileName, jpegFrameData);
     //   File file = new File(Environment.getExternalStorageDirectory(), "temp_usbcamtest1.jpg");
        FileOutputStream fileOutputStream = null;
        try {
            fileOutputStream = new FileOutputStream(file);
            Rect rect = new Rect(0, 0, yuvImage.getWidth(), yuvImage.getHeight());
            log("before compressToJpeg");
            yuvImage.compressToJpeg(new Rect(0, 0, yuvImage.getWidth(), yuvImage.getHeight()), 75, fileOutputStream);
            log("after compressToJpeg");
            fileOutputStream.flush();
        } finally {
            fileOutputStream.close();
        }
    }


    public void processReceivedMJpegVideoFrameKamera(byte[] mjpegFrameData) throws Exception {

        byte[] jpegFrameData = convertMjpegFrameToJpegKamera(mjpegFrameData);

        //   String fileName = new File(Environment.getExternalStorageDirectory(), "temp_usbcamtest1.jpg").getPath();
        //    writeBytesToFile(fileName, jpegFrameData);


        if (bildaufnahme) {
            bildaufnahme = false ;
            date = new Date() ;
            dateFormat = new SimpleDateFormat("yyyy-MM-dd HH-mm-ss") ;

            if (kameramodel == 'm') {
                rootPath = Environment.getExternalStorageDirectory().getAbsolutePath() + "/Microdia/";
            } else {
                rootPath = Environment.getExternalStorageDirectory().getAbsolutePath() + "/PrehKeyTek/"; }


                file = new File(rootPath);
                if (!file.exists()) {
                    file.mkdirs();
                }
                //String fileName = new File(rootPath + String.valueOf(date.getTime()) + ".jpg").getPath();
                String fileName = new File(rootPath + dateFormat.format(date) + ".jpg").getPath() ;
                writeBytesToFile(fileName, jpegFrameData);
        }

        if (stillImageAufnahme) {
            if (stillImage == 1) {
                date = new Date();
                dateFormat = new SimpleDateFormat("yyyy-MM-dd HH-mm-ss") ;
                if (kameramodel == 'm') {
                    rootPath = Environment.getExternalStorageDirectory().getAbsolutePath() + "/Microdia/";
                } else {
                    rootPath = Environment.getExternalStorageDirectory().getAbsolutePath() + "/PrehKeyTek/"; }


                file = new File(rootPath);
                if (!file.exists()) {
                    file.mkdirs();
                }
                //String fileName = new File(rootPath + String.valueOf(date.getTime()) + ".jpg").getPath();
                String fileName = new File(rootPath + dateFormat.format(date) + ".png").getPath() ;
                writeBytesToFile(fileName, mjpegFrameData);
            }
            stillImage++;
            if (stillImage == 2) {
               stillImageAufnahme = false;
               stillImage = 0;
            }
        }
        if (exit == 0) {
            bmp = BitmapFactory.decodeByteArray(jpegFrameData, 0, jpegFrameData.length);
            runOnUiThread(new Runnable() {
                @Override
                public void run() {
                    imageView.setImageBitmap(bmp);
                }
            });
        }
    }

    // see USB video class standard, USB_Video_Payload_MJPEG_1.5.pdf
    private byte[] convertMjpegFrameToJpegKamera(byte[] frameData) throws Exception {
        int frameLen = frameData.length;
        while (frameLen > 0 && frameData[frameLen - 1] == 0) {
            frameLen--;
        }
        //  if (frameLen < 100 || (frameData[0] & 0xff) != 0xff || (frameData[1] & 0xff) != 0xD8 || (frameData[frameLen - 2] & 0xff) != 0xff || (frameData[frameLen - 1] & 0xff) != 0xd9) {
        //        throw new Exception("Invalid MJPEG frame structure, length=" + frameData.length);
        //  }
        boolean hasHuffmanTable = findJpegSegment(frameData, frameLen, 0xC4) != -1;
        exit = 0;
        if (hasHuffmanTable) {
            if (frameData.length == frameLen) {
                return frameData;
            }
            return Arrays.copyOf(frameData, frameLen);
        } else {
            int segmentDaPos = findJpegSegment(frameData, frameLen, 0xDA);

            try {if (segmentDaPos == -1) {
                exit = 1;
            }
            } catch (Exception e) {
                log("Segment 0xDA not found in MJPEG frame data.");}
      //          throw new Exception("Segment 0xDA not found in MJPEG frame data.");
            if (exit ==0) {
                byte[] a = new byte[frameLen + mjpgHuffmanTable.length];
                System.arraycopy(frameData, 0, a, 0, segmentDaPos);
                System.arraycopy(mjpgHuffmanTable, 0, a, segmentDaPos, mjpgHuffmanTable.length);
                System.arraycopy(frameData, segmentDaPos, a, segmentDaPos + mjpgHuffmanTable.length, frameLen - segmentDaPos);
                return a;
            } else
                return null;


        }
    }


    private void processReceivedMJpegVideoFrame(byte[] mjpegFrameData) throws Exception {
        byte[] jpegFrameData = convertMjpegFrameToJpeg(mjpegFrameData);


        Date date = new Date();

        String rootPath = Environment.getExternalStorageDirectory().getAbsolutePath() + "/test/";
        File file = new File(rootPath);
        if (!file.exists()) {
            file.mkdirs();
        }
        String fileName = new File(rootPath + String.valueOf(date.getTime()) + ".jpg").getPath();
        writeBytesToFile(fileName, jpegFrameData);


        final Bitmap bitmap = BitmapFactory.decodeByteArray(jpegFrameData, 0, jpegFrameData.length);
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                imageView.setImageBitmap(bitmap);
            }
        });
    }

    // see USB video class standard, USB_Video_Payload_MJPEG_1.5.pdf
    private byte[] convertMjpegFrameToJpeg(byte[] frameData) throws Exception {
        int frameLen = frameData.length;
        while (frameLen > 0 && frameData[frameLen - 1] == 0) {
            frameLen--;
        }

        try {
            if (frameLen < 100 || (frameData[0] & 0xff) != 0xff || (frameData[1] & 0xff) != 0xD8 || (frameData[frameLen - 2] & 0xff) != 0xff || (frameData[frameLen - 1] & 0xff) != 0xd9)
                ;
        } catch (Exception e) {
            log("Invalid MJPEG frame structure, length=" + frameData.length);
        }

        boolean hasHuffmanTable = findJpegSegment(frameData, frameLen, 0xC4) != -1;
        if (hasHuffmanTable) {
            if (frameData.length == frameLen) {
                return frameData;
            }
            return Arrays.copyOf(frameData, frameLen);
        } else {
            int segmentDaPos = findJpegSegment(frameData, frameLen, 0xDA);
            if (segmentDaPos == -1) {
                throw new Exception("Segment 0xDA not found in MJPEG frame data.");
            }
            byte[] a = new byte[frameLen + mjpgHuffmanTable.length];
            System.arraycopy(frameData, 0, a, 0, segmentDaPos);
            System.arraycopy(mjpgHuffmanTable, 0, a, segmentDaPos, mjpgHuffmanTable.length);
            System.arraycopy(frameData, segmentDaPos, a, segmentDaPos + mjpgHuffmanTable.length, frameLen - segmentDaPos);
            return a;
        }
    }

    private int findJpegSegment(byte[] a, int dataLen, int segmentType) {
        int p = 2;
        while (p <= dataLen - 6) {
            if ((a[p] & 0xff) != 0xff) {
                log("Unexpected JPEG data structure (marker expected).");
                break;
            }
            int markerCode = a[p + 1] & 0xff;
            if (markerCode == segmentType) {
                return p;
            }
            if (markerCode >= 0xD0 && markerCode <= 0xDA) {       // stop when scan data begins
                break;
            }
            int len = ((a[p + 2] & 0xff) << 8) + (a[p + 3] & 0xff);
            p += len + 2;
        }
        return -1;
    }

    private void initStreamingParms() throws Exception {
        final int timeout = 5000;
        int usedStreamingParmsLen;
        int len;
        byte[] streamingParms = new byte[26];
        // The e-com module produces errors with 48 bytes (UVC 1.5) instead of 26 bytes (UVC 1.1) streaming parameters! We could use the USB version info to determine the size of the streaming parameters.
        streamingParms[0] = (byte) 0x01;                // (0x01: dwFrameInterval) //D0: dwFrameInterval //D1: wKeyFrameRate // D2: wPFrameRate // D3: wCompQuality // D4: wCompWindowSize
        streamingParms[2] = (byte) camFormatIndex;                // bFormatIndex
        streamingParms[3] = (byte) camFrameIndex;                 // bFrameIndex
        packUsbInt(camFrameInterval, streamingParms, 4);         // dwFrameInterval
        log("Initial streaming parms: " + dumpStreamingParms(streamingParms));
        len = camDeviceConnection.controlTransfer(RT_CLASS_INTERFACE_SET, SET_CUR, VS_PROBE_CONTROL << 8, camStreamingInterface.getId(), streamingParms, streamingParms.length, timeout);
        if (len != streamingParms.length) {
            throw new Exception("Camera initialization failed. Streaming parms probe set failed, len=" + len + ".");
        }
        // for (int i = 0; i < streamingParms.length; i++) streamingParms[i] = 99;          // temp test
        len = camDeviceConnection.controlTransfer(RT_CLASS_INTERFACE_GET, GET_CUR, VS_PROBE_CONTROL << 8, camStreamingInterface.getId(), streamingParms, streamingParms.length, timeout);
        if (len != streamingParms.length) {
            throw new Exception("Camera initialization failed. Streaming parms probe get failed.");
        }
        log("Probed streaming parms: " + dumpStreamingParms(streamingParms));
        usedStreamingParmsLen = len;
        // log("Streaming parms length: " + usedStreamingParmsLen);
        len = camDeviceConnection.controlTransfer(RT_CLASS_INTERFACE_SET, SET_CUR, VS_COMMIT_CONTROL << 8, camStreamingInterface.getId(), streamingParms, usedStreamingParmsLen, timeout);
        if (len != streamingParms.length) {
            throw new Exception("Camera initialization failed. Streaming parms commit set failed.");
        }
        // for (int i = 0; i < streamingParms.length; i++) streamingParms[i] = 99;          // temp test
        len = camDeviceConnection.controlTransfer(RT_CLASS_INTERFACE_GET, GET_CUR, VS_COMMIT_CONTROL << 8, camStreamingInterface.getId(), streamingParms, usedStreamingParmsLen, timeout);
        if (len != streamingParms.length) {
            throw new Exception("Camera initialization failed. Streaming parms commit get failed.");
        }
        log("Final streaming parms: " + dumpStreamingParms(streamingParms));
    }

    private String dumpStreamingParms(byte[] p) {
        StringBuilder s = new StringBuilder(128);
        s.append("hint=0x" + Integer.toHexString(unpackUsbUInt2(p, 0)));
        s.append(" format=" + (p[2] & 0xf));
        s.append(" frame=" + (p[3] & 0xf));
        s.append(" frameInterval=" + unpackUsbInt(p, 4));
        s.append(" keyFrameRate=" + unpackUsbUInt2(p, 8));
        s.append(" pFrameRate=" + unpackUsbUInt2(p, 10));
        s.append(" compQuality=" + unpackUsbUInt2(p, 12));
        s.append(" compWindowSize=" + unpackUsbUInt2(p, 14));
        s.append(" delay=" + unpackUsbUInt2(p, 16));
        s.append(" maxVideoFrameSize=" + unpackUsbInt(p, 18));
        s.append(" maxPayloadTransferSize=" + unpackUsbInt(p, 22));
        return s.toString();
    }

    private void initStillImageParms() throws Exception {
        final int timeout = 5000;
        int len;
        byte[] parms = new byte[11];
        parms[0] = (byte) camFormatIndex;
        parms[1] = (byte) camFrameIndex;
        parms[2] = 1;
//   len = camDeviceConnection.controlTransfer(RT_CLASS_INTERFACE_GET, SET_CUR, VS_STILL_PROBE_CONTROL << 8, camStreamingInterface.getId(), parms, parms.length, timeout);
//   if (len != parms.length) {
//      throw new Exception("Camera initialization failed. Still image parms probe set failed. len=" + len); }
        len = camDeviceConnection.controlTransfer(RT_CLASS_INTERFACE_GET, GET_CUR, VS_STILL_PROBE_CONTROL << 8, camStreamingInterface.getId(), parms, parms.length, timeout);
        if (len != parms.length) {
            throw new Exception("Camera initialization failed. Still image parms probe get failed.");
        }
        log("Probed still image parms: " + dumpStillImageParms(parms));
        len = camDeviceConnection.controlTransfer(RT_CLASS_INTERFACE_SET, SET_CUR, VS_STILL_COMMIT_CONTROL << 8, camStreamingInterface.getId(), parms, parms.length, timeout);
        if (len != parms.length) {
            throw new Exception("Camera initialization failed. Still image parms commit set failed.");
        }
//   len = camDeviceConnection.controlTransfer(RT_CLASS_INTERFACE_SET, GET_CUR, VS_STILL_COMMIT_CONTROL << 8, camStreamingInterface.getId(), parms, parms.length, timeout);
//   if (len != parms.length) {
//      throw new Exception("Camera initialization failed. Still image parms commit get failed. len=" + len); }
//   log("Final still image parms: " + dumpStillImageParms(parms)); }
    }

    private String dumpStillImageParms(byte[] p) {
        StringBuilder s = new StringBuilder(128);
        s.append("bFormatIndex=" + (p[0] & 0xff));
        s.append(" bFrameIndex=" + (p[1] & 0xff));
        s.append(" bCompressionIndex=" + (p[2] & 0xff));
        s.append(" maxVideoFrameSize=" + unpackUsbInt(p, 3));
        s.append(" maxPayloadTransferSize=" + unpackUsbInt(p, 7));
        return s.toString();
    }

    private static int unpackUsbInt(byte[] buf, int pos) {
        return unpackInt(buf, pos, false);
    }

    private static int unpackUsbUInt2(byte[] buf, int pos) {
        return ((buf[pos + 1] & 0xFF) << 8) | (buf[pos] & 0xFF);
    }

    private static void packUsbInt(int i, byte[] buf, int pos) {
        packInt(i, buf, pos, false);
    }

    private static void packInt(int i, byte[] buf, int pos, boolean bigEndian) {
        if (bigEndian) {
            buf[pos] = (byte) ((i >>> 24) & 0xFF);
            buf[pos + 1] = (byte) ((i >>> 16) & 0xFF);
            buf[pos + 2] = (byte) ((i >>> 8) & 0xFF);
            buf[pos + 3] = (byte) (i & 0xFF);
        } else {
            buf[pos] = (byte) (i & 0xFF);
            buf[pos + 1] = (byte) ((i >>> 8) & 0xFF);
            buf[pos + 2] = (byte) ((i >>> 16) & 0xFF);
            buf[pos + 3] = (byte) ((i >>> 24) & 0xFF);
        }
    }

    private static int unpackInt(byte[] buf, int pos, boolean bigEndian) {
        if (bigEndian) {
            return (buf[pos] << 24) | ((buf[pos + 1] & 0xFF) << 16) | ((buf[pos + 2] & 0xFF) << 8) | (buf[pos + 3] & 0xFF);
        } else {
            return (buf[pos + 3] << 24) | ((buf[pos + 2] & 0xFF) << 16) | ((buf[pos + 1] & 0xFF) << 8) | (buf[pos] & 0xFF);
        }
    }

    private void enableStreaming(boolean enabled) throws Exception {
        enableStreaming_usbFs(enabled);
    }

    private void enableStreaming_usbFs(boolean enabled) throws Exception {
        if (enabled && bulkMode) {
            // clearHalt(camStreamingEndpoint.getAddress());
        }
        int altSetting = enabled ? camStreamingAltSetting : 0;
        // For bulk endpoints, altSetting is always 0.
        usbIso.setInterface(camStreamingInterface.getId(), altSetting);
        if (!enabled) {
            usbIso.flushRequests();
            if (bulkMode) {
                // clearHalt(camStreamingEndpoint.getAddress());
            }
        }
    }

// public void clearHalt (int endpointAddr) throws IOException {
//    IntByReference ep = new IntByReference(endpointAddr);
//    int rc = libc.ioctl(fileDescriptor, USBDEVFS_CLEAR_HALT, ep.getPointer());
//    if (rc != 0) {
//       throw new IOException("ioctl(USBDEVFS_CLEAR_HALT) failed, rc=" + rc + "."); }}

    private void enableStreaming_direct(boolean enabled) throws Exception {
        if (!enabled) {
            return;
        }
        // Ist unklar, wie man das Streaming disabled. AltSetting muss 0 sein damit die Video-Daten kommen.
        int len = camDeviceConnection.controlTransfer(RT_STANDARD_INTERFACE_SET, SET_INTERFACE, 0, camStreamingInterface.getId(), null, 0, 1000);
        if (len != 0) {
            throw new Exception("SET_INTERFACE (direct) failed, len=" + len + ".");
        }
    }

    private void sendStillImageTrigger() throws Exception {
        byte buf[] = new byte[1];
        buf[0] = 1;
        int len = camDeviceConnection.controlTransfer(RT_CLASS_INTERFACE_SET, SET_CUR, VS_STILL_IMAGE_TRIGGER_CONTROL << 8, camStreamingInterface.getId(), buf, 1, 1000);
        if (len != 1) {
            throw new Exception("VS_STILL_IMAGE_TRIGGER_CONTROL failed, len=" + len + ".");
        }
    }

    // Resets the error code after retrieving it.
// Does not work with the e-con camera module!
    private int getVideoControlErrorCode() throws Exception {
        byte buf[] = new byte[1];
        buf[0] = 99;
        int len = camDeviceConnection.controlTransfer(RT_CLASS_INTERFACE_GET, GET_CUR, VC_REQUEST_ERROR_CODE_CONTROL << 8, 0, buf, 1, 1000);
        if (len != 1) {
            throw new Exception("VC_REQUEST_ERROR_CODE_CONTROL failed, len=" + len + ".");
        }
        return buf[0];
    }

    // Does not work with Logitech C310? Always returns 0.
    private int getVideoStreamErrorCode() throws Exception {
        byte buf[] = new byte[1];
        buf[0] = 99;
        int len = camDeviceConnection.controlTransfer(RT_CLASS_INTERFACE_GET, GET_CUR, VS_STREAM_ERROR_CODE_CONTROL << 8, camStreamingInterface.getId(), buf, 1, 1000);
        if (len == 0) {
            return 0;
        }                   // ? (Logitech C310 returns len=0)
        if (len != 1) {
            throw new Exception("VS_STREAM_ERROR_CODE_CONTROL failed, len=" + len + ".");
        }
        return buf[0];
    }

//------------------------------------------------------------------------------

    private static String hexDump(byte[] buf, int len) {
        StringBuilder s = new StringBuilder(len * 3);
        for (int p = 0; p < len; p++) {
            if (p > 0) {
                s.append(' ');
            }
            int v = buf[p] & 0xff;
            if (v < 16) {
                s.append('0');
            }
            s.append(Integer.toHexString(v));
        }
        return s.toString();
    }

    public void displayMessage(final String msg) {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                Toast.makeText(Main.this, msg, Toast.LENGTH_LONG).show();
            }
        });
    }

    public void log(String msg) {
        Log.i("UVC_Camera", msg);
    }

    public void displayErrorMessage(Throwable e) {
        Log.e("UVC_Camera", "Error in MainActivity", e);
        displayMessage("Error: " + e);
    }

    private void startBackgroundJob(final Callable callable) throws Exception {
        if (backgroundJobActive) {
            throw new Exception("Background job is already active.");
        }
        backgroundJobActive = true;
        Thread thread = new Thread() {
            @Override
            public void run() {
                try {
                    callable.call();
                } catch (Throwable e) {
                    displayErrorMessage(e);
                } finally {
                    backgroundJobActive = false;
                }
            }
        };
        thread.setPriority(Thread.MAX_PRIORITY);
        thread.start();
    }


    // see 10918-1:1994, K.3.3.1 Specification of typical tables for DC difference coding
    private static byte[] mjpgHuffmanTable = {
            (byte) 0xff, (byte) 0xc4, (byte) 0x01, (byte) 0xa2, (byte) 0x00, (byte) 0x00, (byte) 0x01, (byte) 0x05, (byte) 0x01, (byte) 0x01,
            (byte) 0x01, (byte) 0x01, (byte) 0x01, (byte) 0x01, (byte) 0x00, (byte) 0x00, (byte) 0x00, (byte) 0x00, (byte) 0x00, (byte) 0x00,
            (byte) 0x00, (byte) 0x00, (byte) 0x01, (byte) 0x02, (byte) 0x03, (byte) 0x04, (byte) 0x05, (byte) 0x06, (byte) 0x07, (byte) 0x08,
            (byte) 0x09, (byte) 0x0a, (byte) 0x0b, (byte) 0x10, (byte) 0x00, (byte) 0x02, (byte) 0x01, (byte) 0x03, (byte) 0x03, (byte) 0x02,
            (byte) 0x04, (byte) 0x03, (byte) 0x05, (byte) 0x05, (byte) 0x04, (byte) 0x04, (byte) 0x00, (byte) 0x00, (byte) 0x01, (byte) 0x7d,
            (byte) 0x01, (byte) 0x02, (byte) 0x03, (byte) 0x00, (byte) 0x04, (byte) 0x11, (byte) 0x05, (byte) 0x12, (byte) 0x21, (byte) 0x31,
            (byte) 0x41, (byte) 0x06, (byte) 0x13, (byte) 0x51, (byte) 0x61, (byte) 0x07, (byte) 0x22, (byte) 0x71, (byte) 0x14, (byte) 0x32,
            (byte) 0x81, (byte) 0x91, (byte) 0xa1, (byte) 0x08, (byte) 0x23, (byte) 0x42, (byte) 0xb1, (byte) 0xc1, (byte) 0x15, (byte) 0x52,
            (byte) 0xd1, (byte) 0xf0, (byte) 0x24, (byte) 0x33, (byte) 0x62, (byte) 0x72, (byte) 0x82, (byte) 0x09, (byte) 0x0a, (byte) 0x16,
            (byte) 0x17, (byte) 0x18, (byte) 0x19, (byte) 0x1a, (byte) 0x25, (byte) 0x26, (byte) 0x27, (byte) 0x28, (byte) 0x29, (byte) 0x2a,
            (byte) 0x34, (byte) 0x35, (byte) 0x36, (byte) 0x37, (byte) 0x38, (byte) 0x39, (byte) 0x3a, (byte) 0x43, (byte) 0x44, (byte) 0x45,
            (byte) 0x46, (byte) 0x47, (byte) 0x48, (byte) 0x49, (byte) 0x4a, (byte) 0x53, (byte) 0x54, (byte) 0x55, (byte) 0x56, (byte) 0x57,
            (byte) 0x58, (byte) 0x59, (byte) 0x5a, (byte) 0x63, (byte) 0x64, (byte) 0x65, (byte) 0x66, (byte) 0x67, (byte) 0x68, (byte) 0x69,
            (byte) 0x6a, (byte) 0x73, (byte) 0x74, (byte) 0x75, (byte) 0x76, (byte) 0x77, (byte) 0x78, (byte) 0x79, (byte) 0x7a, (byte) 0x83,
            (byte) 0x84, (byte) 0x85, (byte) 0x86, (byte) 0x87, (byte) 0x88, (byte) 0x89, (byte) 0x8a, (byte) 0x92, (byte) 0x93, (byte) 0x94,
            (byte) 0x95, (byte) 0x96, (byte) 0x97, (byte) 0x98, (byte) 0x99, (byte) 0x9a, (byte) 0xa2, (byte) 0xa3, (byte) 0xa4, (byte) 0xa5,
            (byte) 0xa6, (byte) 0xa7, (byte) 0xa8, (byte) 0xa9, (byte) 0xaa, (byte) 0xb2, (byte) 0xb3, (byte) 0xb4, (byte) 0xb5, (byte) 0xb6,
            (byte) 0xb7, (byte) 0xb8, (byte) 0xb9, (byte) 0xba, (byte) 0xc2, (byte) 0xc3, (byte) 0xc4, (byte) 0xc5, (byte) 0xc6, (byte) 0xc7,
            (byte) 0xc8, (byte) 0xc9, (byte) 0xca, (byte) 0xd2, (byte) 0xd3, (byte) 0xd4, (byte) 0xd5, (byte) 0xd6, (byte) 0xd7, (byte) 0xd8,
            (byte) 0xd9, (byte) 0xda, (byte) 0xe1, (byte) 0xe2, (byte) 0xe3, (byte) 0xe4, (byte) 0xe5, (byte) 0xe6, (byte) 0xe7, (byte) 0xe8,
            (byte) 0xe9, (byte) 0xea, (byte) 0xf1, (byte) 0xf2, (byte) 0xf3, (byte) 0xf4, (byte) 0xf5, (byte) 0xf6, (byte) 0xf7, (byte) 0xf8,
            (byte) 0xf9, (byte) 0xfa, (byte) 0x01, (byte) 0x00, (byte) 0x03, (byte) 0x01, (byte) 0x01, (byte) 0x01, (byte) 0x01, (byte) 0x01,
            (byte) 0x01, (byte) 0x01, (byte) 0x01, (byte) 0x01, (byte) 0x00, (byte) 0x00, (byte) 0x00, (byte) 0x00, (byte) 0x00, (byte) 0x00,
            (byte) 0x01, (byte) 0x02, (byte) 0x03, (byte) 0x04, (byte) 0x05, (byte) 0x06, (byte) 0x07, (byte) 0x08, (byte) 0x09, (byte) 0x0a,
            (byte) 0x0b, (byte) 0x11, (byte) 0x00, (byte) 0x02, (byte) 0x01, (byte) 0x02, (byte) 0x04, (byte) 0x04, (byte) 0x03, (byte) 0x04,
            (byte) 0x07, (byte) 0x05, (byte) 0x04, (byte) 0x04, (byte) 0x00, (byte) 0x01, (byte) 0x02, (byte) 0x77, (byte) 0x00, (byte) 0x01,
            (byte) 0x02, (byte) 0x03, (byte) 0x11, (byte) 0x04, (byte) 0x05, (byte) 0x21, (byte) 0x31, (byte) 0x06, (byte) 0x12, (byte) 0x41,
            (byte) 0x51, (byte) 0x07, (byte) 0x61, (byte) 0x71, (byte) 0x13, (byte) 0x22, (byte) 0x32, (byte) 0x81, (byte) 0x08, (byte) 0x14,
            (byte) 0x42, (byte) 0x91, (byte) 0xa1, (byte) 0xb1, (byte) 0xc1, (byte) 0x09, (byte) 0x23, (byte) 0x33, (byte) 0x52, (byte) 0xf0,
            (byte) 0x15, (byte) 0x62, (byte) 0x72, (byte) 0xd1, (byte) 0x0a, (byte) 0x16, (byte) 0x24, (byte) 0x34, (byte) 0xe1, (byte) 0x25,
            (byte) 0xf1, (byte) 0x17, (byte) 0x18, (byte) 0x19, (byte) 0x1a, (byte) 0x26, (byte) 0x27, (byte) 0x28, (byte) 0x29, (byte) 0x2a,
            (byte) 0x35, (byte) 0x36, (byte) 0x37, (byte) 0x38, (byte) 0x39, (byte) 0x3a, (byte) 0x43, (byte) 0x44, (byte) 0x45, (byte) 0x46,
            (byte) 0x47, (byte) 0x48, (byte) 0x49, (byte) 0x4a, (byte) 0x53, (byte) 0x54, (byte) 0x55, (byte) 0x56, (byte) 0x57, (byte) 0x58,
            (byte) 0x59, (byte) 0x5a, (byte) 0x63, (byte) 0x64, (byte) 0x65, (byte) 0x66, (byte) 0x67, (byte) 0x68, (byte) 0x69, (byte) 0x6a,
            (byte) 0x73, (byte) 0x74, (byte) 0x75, (byte) 0x76, (byte) 0x77, (byte) 0x78, (byte) 0x79, (byte) 0x7a, (byte) 0x82, (byte) 0x83,
            (byte) 0x84, (byte) 0x85, (byte) 0x86, (byte) 0x87, (byte) 0x88, (byte) 0x89, (byte) 0x8a, (byte) 0x92, (byte) 0x93, (byte) 0x94,
            (byte) 0x95, (byte) 0x96, (byte) 0x97, (byte) 0x98, (byte) 0x99, (byte) 0x9a, (byte) 0xa2, (byte) 0xa3, (byte) 0xa4, (byte) 0xa5,
            (byte) 0xa6, (byte) 0xa7, (byte) 0xa8, (byte) 0xa9, (byte) 0xaa, (byte) 0xb2, (byte) 0xb3, (byte) 0xb4, (byte) 0xb5, (byte) 0xb6,
            (byte) 0xb7, (byte) 0xb8, (byte) 0xb9, (byte) 0xba, (byte) 0xc2, (byte) 0xc3, (byte) 0xc4, (byte) 0xc5, (byte) 0xc6, (byte) 0xc7,
            (byte) 0xc8, (byte) 0xc9, (byte) 0xca, (byte) 0xd2, (byte) 0xd3, (byte) 0xd4, (byte) 0xd5, (byte) 0xd6, (byte) 0xd7, (byte) 0xd8,
            (byte) 0xd9, (byte) 0xda, (byte) 0xe2, (byte) 0xe3, (byte) 0xe4, (byte) 0xe5, (byte) 0xe6, (byte) 0xe7, (byte) 0xe8, (byte) 0xe9,
            (byte) 0xea, (byte) 0xf2, (byte) 0xf3, (byte) 0xf4, (byte) 0xf5, (byte) 0xf6, (byte) 0xf7, (byte) 0xf8, (byte) 0xf9, (byte) 0xfa};

}
