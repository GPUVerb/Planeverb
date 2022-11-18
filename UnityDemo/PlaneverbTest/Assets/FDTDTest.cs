using System.Collections;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using UnityEngine;

public class FDTDTest : MonoBehaviour
{
    [DllImport("ProjectPlaneverbUnityPlugin")]
    static extern float PlaneverbGetResponsePressure(float x, float z);

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

    List<GameObject> cubes = new List<GameObject>();

    // Start is called before the first frame update
    void Start()
    {
        for(float x = minCorner.x; x <= maxCorner.x; x += cubeSize)
        {
            for(float y = minCorner.y; y <= maxCorner.y; y += cubeSize)
            {
                GameObject obj = GameObject.CreatePrimitive(PrimitiveType.Cube);
                obj.transform.position = new Vector3(x, 1, y);
                obj.transform.localScale = Vector3.one * cubeSize;

                obj.GetComponent<Collider>().enabled = false;

                cubes.Add(obj);
            }
        }
    }

    // Update is called once per frame
    void Update()
    {
        int k = 0;
        for (float x = minCorner.x; x <= maxCorner.x; x += cubeSize)
        {
            for (float y = minCorner.y; y <= maxCorner.y; y += cubeSize)
            {
                float pr = PlaneverbGetResponsePressure(x, y);
                cubes[k++].transform.position = new Vector3(x, baseHeight + scale * pr, y);
            }
        }
    }
}
