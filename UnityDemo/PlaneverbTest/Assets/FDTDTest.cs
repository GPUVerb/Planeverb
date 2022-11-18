using System;
using System.Collections;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using UnityEngine;

public class FDTDTest : MonoBehaviour
{
    [DllImport("ProjectPlaneverbUnityPlugin")]
    static extern int PlaneverbGetResponsePressure(float x, float z, IntPtr result);

    [SerializeField]
    Vector2 minCorner = new Vector2(0, 0);

    [SerializeField]
    Vector2 maxCorner = new Vector2(9, 9);

    [SerializeField]
    float cubeSize = 0.1f;
    [SerializeField]
    float baseHeight = 1;
    [SerializeField]
    float scale = 100;
    [SerializeField]
    float simTime = 4f;

    class Info
    {
        public GameObject ins;
        public float[] data;
        public int numSamples;
    }

    List<Info> cubes = new List<Info>();
    int numSamples = 0;
    float perSampleTime;


    float curTime = 0;

    void Start()
    {
        for (float x = minCorner.x; x <= maxCorner.x; x += cubeSize)
        {
            for (float y = minCorner.y; y <= maxCorner.y; y += cubeSize)
            {
                GameObject obj = GameObject.CreatePrimitive(PrimitiveType.Cube);
                obj.transform.position = new Vector3(x, baseHeight, y);
                obj.transform.localScale = Vector3.one * cubeSize;
                obj.GetComponent<Collider>().enabled = false;

                Info info = new Info();
                info.ins = obj;
                info.data = new float[5000];
                unsafe
                {
                    fixed (float* ptr = info.data)
                    {
                        info.numSamples = PlaneverbGetResponsePressure(x, y, (IntPtr)ptr);
                        numSamples = Mathf.Max(info.numSamples, numSamples);
                    }
                }
                cubes.Add(info);
            }
        }

        if(numSamples != 0)
        {
            perSampleTime = simTime / numSamples;
        }
    }

    void UpdateData()
    {
        int i = 0;
        for (float x = minCorner.x; x <= maxCorner.x; x += cubeSize)
        {
            for (float y = minCorner.y; y <= maxCorner.y; y += cubeSize)
            {
                Info info = cubes[i++];
                unsafe
                {
                    fixed (float* ptr = info.data)
                    {
                        info.numSamples = PlaneverbGetResponsePressure(x, y, (IntPtr)ptr);
                        numSamples = Mathf.Max(info.numSamples, numSamples);
                    }
                }
            }
        }

        if (numSamples != 0)
        {
            perSampleTime = simTime / numSamples;
        }
    }

    private void Update()
    {
        if(numSamples == 0)
        {
            return;
        }


        int sample = Mathf.FloorToInt(curTime / perSampleTime);
        if(sample >= numSamples)
        {
            UpdateData();
            curTime = 0;
            return;
        }


        int i = 0;
        for (float x = minCorner.x; x <= maxCorner.x; x += cubeSize)
        {
            for (float y = minCorner.y; y <= maxCorner.y; y += cubeSize)
            {
                Info info = cubes[i++];
                if(sample < info.numSamples)
                {
                    info.ins.transform.position = new Vector3(x, baseHeight + scale * info.data[sample], y);
                }
                else
                {
                    info.ins.transform.position = new Vector3(x, baseHeight, y);
                }
            }
        }

        curTime += Time.deltaTime;
    }
}
